/**
 * @file    hal_motor_executor.c
 * @brief   电机执行器组合件：事件队列、槽池与 provider 注册
 *
 * 采用显式状态机驱动，逐 tick 推进。除急停外命令均为异步语义。
 * 关键控制逻辑（启动/换向/停止/故障/急停/看门狗）均以状态迁移表达，
 * 上电默认态与故障安全态均为“停止且输出关断”。
 * 出站端口符号由 hal_motor_exec_port.c 统一分派，本文件提供每实例 ops 与槽池。
 */
#include "adapters/outbound/hal/components/motor_exec/hal_motor_executor.h"

#include "adapters/outbound/hal/components/motor_exec/hal_motor_executor_internal.h"
#include "domain/ports/outbound/motor/hal_motor_exec_provider.h"

#include <string.h>

typedef struct {
    bool             bound;
    motor_executor_t executor;
    hal_motor_exec_t handle;
} motor_executor_slot_t;

static motor_executor_slot_t s_slots[WDF_MOTOR_EXECUTOR_INSTANCE_COUNT];

/* ------------------------- 事件队列（环形缓冲） ------------------------- */

/**
 * @brief  删除相对 head 偏移 drop 处的事件，后续项前移
 */
static void ev_remove_at(motor_executor_t *e, int drop)
{
    int m;

    for (m = drop; m < e->ev_count - 1; ++m) {
        int from = (e->ev_head + m + 1) % MOTOR_EVENT_QUEUE_CAP;
        int to   = (e->ev_head + m) % MOTOR_EVENT_QUEUE_CAP;

        e->events[to] = e->events[from];
    }
    e->ev_count--;
}

/**
 * @brief  压入事件；满时优先丢弃同电机最旧项，避免无人消费的轴挤掉其它轴结局
 * @note   若队列中尚无该电机事件，再退回丢弃全局最旧。
 */
static void ev_push(motor_executor_t *e, const hal_motor_event_t *ev)
{
    if (e->ev_count >= MOTOR_EVENT_QUEUE_CAP) {
        int drop = 0;
        int n;

        for (n = 0; n < e->ev_count; ++n) {
            int idx = (e->ev_head + n) % MOTOR_EVENT_QUEUE_CAP;

            if (e->events[idx].motor == ev->motor) {
                drop = n;
                break;
            }
        }
        ev_remove_at(e, drop);
    }
    {
        int tail        = (e->ev_head + e->ev_count) % MOTOR_EVENT_QUEUE_CAP;
        e->events[tail] = *ev;
        e->ev_count++;
    }
}

/**
 * @brief  本次运行的累计时长
 * @note   run 型（无结束条件）与 move 型同样在 begin_start 记录 move_start_ms，
 *         耗时对两者都有意义，故不以 move_active 为条件，否则 run 型恒为 0。
 */
static uint64_t eff_elapsed(motor_executor_t *e, int i)
{
    motor_mstate_t *s = &e->m[i];
    if (s->phase == HAL_MOTOR_PHASE_RUNNING) {
        return s->elapsed_ms + (e->now - s->move_start_ms);
    }
    return s->elapsed_ms;
}

/**
 * @brief  离开 RUNNING 时冻结已跑时长，供随后事件与停机路径读取
 * @note   非 RUNNING 时为空操作。RUNNING 时累加后将起点推到 now，可安全重复调用
 *         （ORIGIN 路径会 settle 后再 finish_halt 内再 settle，二次加 0）。
 */
void settle_elapsed(motor_executor_t *e, int i)
{
    motor_mstate_t *s = &e->m[i];
    if (s->phase == HAL_MOTOR_PHASE_RUNNING) {
        s->elapsed_ms += e->now - s->move_start_ms;
        s->move_start_ms = e->now;
    }
}

void push_event(motor_executor_t         *e,
                       int                       i,
                       hal_motor_event_type_t    t,
                       hal_motor_end_condition_t trig,
                       hal_motor_fault_code_t    fc)
{
    hal_motor_event_t ev;

    ev.motor   = i;
    ev.type    = t;
    ev.trigger = trig;
    /* 限位种类取本次实际触发的硬限位，仅在确实由限位终止时有效。 */
    ev.has_limit  = (trig == HAL_MOTOR_END_LIMIT);
    ev.limit      = e->m[i].end_limit;
    ev.final_pos  = e->m[i].position;
    ev.elapsed_ms = eff_elapsed(e, i);
    ev.fault      = fc;
    ev_push(e, &ev);
}

/* ------------------------- 事件分发 ------------------------- */

void motor_dispatch(motor_executor_t *e)
{
    if (!e->cb) {
        return;
    }
    e->in_dispatch = true;
    hal_motor_event_t ev;
    while (e->ev_count > 0) {
        ev         = e->events[e->ev_head];
        e->ev_head = (e->ev_head + 1) % MOTOR_EVENT_QUEUE_CAP;
        e->ev_count--;
        e->cb(&ev, e->cb_ctx);
    }
    e->in_dispatch = false;
}

/* ------------------------- 事件 ------------------------- */

static void motor_set_event_callback(motor_executor_t *e, motor_event_cb_t cb, void *ctx)
{
    if (e->in_dispatch) {
        return;
    }
    e->cb     = cb;
    e->cb_ctx = ctx;
}

static bool motor_pop_event(motor_executor_t *e, hal_motor_event_t *out)
{
    if (e->ev_count <= 0) {
        return false;
    }
    *out       = e->events[e->ev_head];
    e->ev_head = (e->ev_head + 1) % MOTOR_EVENT_QUEUE_CAP;
    e->ev_count--;
    return true;
}

static bool motor_pop_event_for(motor_executor_t *e, int motor, hal_motor_event_t *out)
{
    int n;

    if (e->ev_count <= 0) {
        return false;
    }
    for (n = 0; n < e->ev_count; ++n) {
        int idx = (e->ev_head + n) % MOTOR_EVENT_QUEUE_CAP;

        if (e->events[idx].motor != motor) {
            continue;
        }
        *out = e->events[idx];
        ev_remove_at(e, n);
        return true;
    }
    return false;
}

/* ------------------------- provider 分派 ------------------------- */

static hal_motor_cmd_result_t provider_run(void                        *ctx,
                                           int                          motor,
                                           hal_motor_speed_t            speed,
                                           hal_motor_dir_t              dir,
                                           const hal_motor_move_spec_t *spec)
{
    return motor_run((motor_executor_t *)ctx, motor, speed, dir, spec);
}

static hal_motor_cmd_result_t provider_stop(void *ctx, int motor)
{
    return motor_stop((motor_executor_t *)ctx, motor);
}

static hal_motor_cmd_result_t provider_home(void *ctx, int motor)
{
    return motor_home((motor_executor_t *)ctx, motor);
}

static hal_motor_cmd_result_t provider_recover(void *ctx, int motor, hal_motor_recovery_step_t step)
{
    return motor_recover((motor_executor_t *)ctx, motor, step);
}

static hal_motor_phase_t provider_phase(const void *ctx, int motor)
{
    return motor_phase((const motor_executor_t *)ctx, motor);
}

static int64_t provider_position(const void *ctx, int motor)
{
    return motor_position((const motor_executor_t *)ctx, motor);
}

static hal_motor_dir_t provider_direction(const void *ctx, int motor)
{
    return motor_direction((const motor_executor_t *)ctx, motor);
}

static hal_motor_fault_code_t provider_fault_code(const void *ctx, int motor)
{
    return motor_fault_code((const motor_executor_t *)ctx, motor);
}

static bool provider_encoder_healthy(const void *ctx, int motor)
{
    return motor_encoder_healthy((const motor_executor_t *)ctx, motor);
}

static bool provider_baseline_trusted(const void *ctx, int motor)
{
    return motor_baseline_trusted((const motor_executor_t *)ctx, motor);
}

static bool provider_pop_event(void *ctx, hal_motor_event_t *out)
{
    return motor_pop_event((motor_executor_t *)ctx, out);
}

static bool provider_pop_event_for(void *ctx, int motor, hal_motor_event_t *out)
{
    return motor_pop_event_for((motor_executor_t *)ctx, motor, out);
}

static const hal_motor_exec_ops_t s_provider_ops = {
    .run              = provider_run,
    .stop             = provider_stop,
    .home             = provider_home,
    .recover          = provider_recover,
    .phase            = provider_phase,
    .position         = provider_position,
    .direction        = provider_direction,
    .fault_code       = provider_fault_code,
    .encoder_healthy  = provider_encoder_healthy,
    .baseline_trusted = provider_baseline_trusted,
    .pop_event        = provider_pop_event,
    .pop_event_for    = provider_pop_event_for,
};

/* ------------------------- 槽池生命周期 ------------------------- */

static motor_executor_t *executor_from_handle(hal_motor_exec_t *exec)
{
    unsigned slot_id;

    for (slot_id = 0U; slot_id < WDF_MOTOR_EXECUTOR_INSTANCE_COUNT; ++slot_id) {
        if (s_slots[slot_id].bound && (&s_slots[slot_id].handle == exec)) {
            return &s_slots[slot_id].executor;
        }
    }
    return NULL;
}

static const motor_executor_t *const_executor_from_handle(const hal_motor_exec_t *exec)
{
    unsigned slot_id;

    for (slot_id = 0U; slot_id < WDF_MOTOR_EXECUTOR_INSTANCE_COUNT; ++slot_id) {
        if (s_slots[slot_id].bound && (&s_slots[slot_id].handle == exec)) {
            return &s_slots[slot_id].executor;
        }
    }
    return NULL;
}

static const char *ports_error(const motor_config_t *cfg, const motor_ports_t *ports)
{
    int driver;
    int motor;

    if ((cfg == NULL) || (ports == NULL)) {
        return "config or ports missing";
    }
    if ((ports->clock == NULL) || (ports->clock->now_ms == NULL)) {
        return "clock port missing";
    }
    if ((ports->drivers == NULL) || (ports->encoders == NULL)) {
        return "motor port arrays missing";
    }
    if ((ports->sensors == NULL) || (ports->sensors->limit == NULL)) {
        return "sensor port missing";
    }
    if ((ports->estop == NULL) || (ports->estop->active == NULL)) {
        return "estop port missing";
    }
    if ((cfg->driver_count <= 0) || (cfg->driver_count > MOTOR_MAX_DRIVERS)) {
        return NULL;
    }
    for (driver = 0; driver < cfg->driver_count; ++driver) {
        const motor_driver_t *port = ports->drivers[driver];

        if ((port == NULL) || (port->set_output == NULL) || (port->cutoff == NULL) || (port->reset == NULL)
            || (port->is_running == NULL) || (port->current == NULL)) {
            return "driver port incomplete";
        }
    }
    if ((cfg->motor_count <= 0) || (cfg->motor_count > MOTOR_MAX_MOTORS)) {
        return NULL;
    }
    for (motor = 0; motor < cfg->motor_count; ++motor) {
        const motor_encoder_t *encoder = ports->encoders[motor];

        if (cfg->motors[motor].has_encoder
            && ((encoder == NULL) || (encoder->raw == NULL) || (encoder->zero == NULL))) {
            return "encoder port incomplete";
        }
    }
    return NULL;
}

motor_init_result_t motor_executor_bind(unsigned              slot_id,
                                        const motor_config_t *cfg,
                                        const motor_ports_t  *ports,
                                        hal_motor_exec_t    **out_exec)
{
    motor_executor_slot_t *slot;
    motor_init_result_t    result;
    const char            *error;

    if (out_exec == NULL) {
        return init_err("out executor missing");
    }
    *out_exec = NULL;
    if (slot_id >= WDF_MOTOR_EXECUTOR_INSTANCE_COUNT) {
        return init_err("slot id out of range");
    }
    slot = &s_slots[slot_id];
    if (slot->bound) {
        return init_err("slot already bound");
    }
    error = ports_error(cfg, ports);
    if (error != NULL) {
        return init_err(error);
    }

    memset(slot, 0, sizeof(*slot));
    result = motor_init(&slot->executor, cfg, ports);
    if (!result.ok) {
        memset(slot, 0, sizeof(*slot));
        return result;
    }
    if (!hal_motor_exec_provider_bind(&slot->handle, &s_provider_ops, &slot->executor)) {
        memset(slot, 0, sizeof(*slot));
        return init_err("provider binding failed");
    }
    slot->bound = true;
    *out_exec   = &slot->handle;
    return result;
}

motor_init_result_t motor_executor_reinit(hal_motor_exec_t *exec)
{
    motor_executor_t *executor = executor_from_handle(exec);

    return (executor != NULL) ? motor_reinit(executor) : init_err("executor unavailable");
}

void motor_executor_tick(hal_motor_exec_t *exec)
{
    motor_executor_t *executor = executor_from_handle(exec);

    if (executor != NULL) {
        motor_tick(executor);
    }
}

hal_motor_cmd_result_t motor_executor_zero_encoder(hal_motor_exec_t *exec, int motor)
{
    motor_executor_t *executor = executor_from_handle(exec);

    return (executor != NULL) ? motor_zero_encoder(executor, motor) : cmd_reject("executor-unavailable");
}

hal_motor_cmd_result_t motor_executor_confirm_baseline(hal_motor_exec_t *exec, int motor)
{
    motor_executor_t *executor = executor_from_handle(exec);

    return (executor != NULL) ? motor_confirm_baseline(executor, motor) : cmd_reject("executor-unavailable");
}

void motor_executor_reset_estop(hal_motor_exec_t *exec)
{
    motor_executor_t *executor = executor_from_handle(exec);

    if (executor != NULL) {
        motor_reset_estop(executor);
    }
}

void motor_executor_reset_watchdog(hal_motor_exec_t *exec)
{
    motor_executor_t *executor = executor_from_handle(exec);

    if (executor != NULL) {
        motor_reset_watchdog(executor);
    }
}

int motor_executor_current_freq(const hal_motor_exec_t *exec, int motor)
{
    const motor_executor_t *executor = const_executor_from_handle(exec);

    return (executor != NULL) ? motor_current_freq(executor, motor) : 0;
}

bool motor_executor_in_safe_state(const hal_motor_exec_t *exec)
{
    const motor_executor_t *executor = const_executor_from_handle(exec);

    return (executor != NULL) && motor_in_safe_state(executor);
}

void motor_executor_set_event_callback(hal_motor_exec_t *exec, motor_event_cb_t cb, void *ctx)
{
    motor_executor_t *executor = executor_from_handle(exec);

    if (executor != NULL) {
        motor_set_event_callback(executor, cb, ctx);
    }
}

#ifdef HAL_MOTOR_EXECUTOR_UNIT_TEST
void motor_executor_test_reset(void)
{
    memset(s_slots, 0, sizeof(s_slots));
}
#endif
