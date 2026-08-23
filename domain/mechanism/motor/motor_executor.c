/**
 * @file    motor_executor.c
 * @brief   电机执行器组合件：事件队列、槽池与 provider 注册
 *
 * 采用显式状态机驱动，逐 tick 推进。除急停外命令均为异步语义。
 * 关键控制逻辑（启动/换向/停止/故障/急停/看门狗）均以状态迁移表达，
 * 上电默认态与故障安全态均为“停止且输出关断”。
 * 出站端口符号由 motor_exec_port.c 统一分派，本文件提供每实例 ops 与槽池。
 */
#include "domain/mechanism/motor/motor_executor.h"

#include "domain/mechanism/motor/motor_executor_internal.h"
#include "domain/ports/outbound/motor/motor_exec_provider.h"

#include <string.h>

typedef struct {
    bool             bound;
    motor_executor_t executor;
    motor_exec_t handle;
} motor_executor_slot_t;

static motor_executor_slot_t s_slots[WDF_MOTOR_EXECUTOR_INSTANCE_COUNT];

/* 命令与 tick 串行化；急停切断路径不取此锁。 */

void motor_lock_init(motor_executor_t *e)
{
    pthread_mutexattr_t attr;

    if ((e == NULL) || e->lock_ready) {
        return;
    }
    if (pthread_mutexattr_init(&attr) != 0) {
        return;
    }
    if (pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE) == 0) {
        if (pthread_mutex_init(&e->lock, &attr) == 0) {
            e->lock_ready = true;
        }
    }
    (void)pthread_mutexattr_destroy(&attr);
}

void motor_lock_destroy(motor_executor_t *e)
{
    if ((e != NULL) && e->lock_ready) {
        (void)pthread_mutex_destroy(&e->lock);
        e->lock_ready = false;
    }
}

void motor_lock(motor_executor_t *e)
{
    if ((e != NULL) && e->lock_ready) {
        (void)pthread_mutex_lock(&e->lock);
    }
}

void motor_unlock(motor_executor_t *e)
{
    if ((e != NULL) && e->lock_ready) {
        (void)pthread_mutex_unlock(&e->lock);
    }
}

static void ev_remove_at_slot(motor_event_slot_t *slot, int drop)
{
    int m;

    for (m = drop; m < slot->count - 1; ++m) {
        int from = (slot->head + m + 1) % MOTOR_EVENT_SLOT_CAP;
        int to   = (slot->head + m) % MOTOR_EVENT_SLOT_CAP;

        slot->q[to] = slot->q[from];
    }
    slot->count--;
}

static void ev_push(motor_executor_t *e, const motor_event_t *ev)
{
    motor_event_slot_t *slot;

    if ((ev->motor < 0) || (ev->motor >= e->motor_count)) {
        return;
    }
    slot = &e->ev[ev->motor];
    if (slot->count >= MOTOR_EVENT_SLOT_CAP) {
        ev_remove_at_slot(slot, 0);
    }
    {
        int tail      = (slot->head + slot->count) % MOTOR_EVENT_SLOT_CAP;
        slot->q[tail] = *ev;
        slot->count++;
    }
}

static uint64_t eff_elapsed(motor_executor_t *e, int i)
{
    motor_mstate_t *s = &e->m[i];
    if (s->phase == MOTOR_PHASE_RUNNING) {
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
    if (s->phase == MOTOR_PHASE_RUNNING) {
        s->elapsed_ms += e->now - s->move_start_ms;
        s->move_start_ms = e->now;
    }
}

void push_event(motor_executor_t         *e,
                       int                       i,
                       motor_event_type_t    t,
                       motor_end_condition_t trig,
                       motor_exec_fault_code_t    fc)
{
    motor_event_t ev;

    ev.motor   = i;
    ev.type    = t;
    ev.trigger = trig;
    /* 限位种类取本次实际触发的硬限位，仅在确实由限位终止时有效。 */
    ev.has_limit  = (trig == MOTOR_END_LIMIT);
    ev.limit      = e->m[i].end_limit;
    ev.final_pos  = e->m[i].position;
    ev.elapsed_ms = eff_elapsed(e, i);
    ev.fault      = fc;
    ev_push(e, &ev);
}

static bool motor_pop_event(motor_executor_t *e, motor_event_t *out)
{
    int i;

    motor_lock(e);
    for (i = 0; i < e->motor_count; ++i) {
        motor_event_slot_t *slot = &e->ev[i];

        if (slot->count <= 0) {
            continue;
        }
        *out       = slot->q[slot->head];
        slot->head = (slot->head + 1) % MOTOR_EVENT_SLOT_CAP;
        slot->count--;
        motor_unlock(e);
        return true;
    }
    motor_unlock(e);
    return false;
}

static bool motor_pop_event_for(motor_executor_t *e, int motor, motor_event_t *out)
{
    motor_event_slot_t *slot;

    if ((motor < 0) || (motor >= e->motor_count)) {
        return false;
    }
    motor_lock(e);
    slot = &e->ev[motor];
    if (slot->count <= 0) {
        motor_unlock(e);
        return false;
    }
    *out       = slot->q[slot->head];
    slot->head = (slot->head + 1) % MOTOR_EVENT_SLOT_CAP;
    slot->count--;
    motor_unlock(e);
    return true;
}

/* ------------------------- provider 分派 ------------------------- */

static motor_cmd_result_t provider_run(void                        *ctx,
                                           int                          motor,
                                           motor_speed_t            speed,
                                           motor_dir_t              dir,
                                           const motor_move_spec_t *spec)
{
    return motor_run((motor_executor_t *)ctx, motor, speed, dir, spec);
}

static motor_cmd_result_t provider_stop(void *ctx, int motor)
{
    return motor_stop((motor_executor_t *)ctx, motor);
}

static motor_cmd_result_t provider_home(void *ctx, int motor)
{
    return motor_home((motor_executor_t *)ctx, motor);
}

static motor_cmd_result_t provider_recover(void *ctx, int motor, motor_exec_recovery_step_t step)
{
    return motor_recover((motor_executor_t *)ctx, motor, step);
}

static motor_exec_phase_t provider_phase(const void *ctx, int motor)
{
    return motor_phase((const motor_executor_t *)ctx, motor);
}

static int64_t provider_position(const void *ctx, int motor)
{
    return motor_position((const motor_executor_t *)ctx, motor);
}

static motor_dir_t provider_direction(const void *ctx, int motor)
{
    return motor_direction((const motor_executor_t *)ctx, motor);
}

static motor_exec_fault_code_t provider_fault_code(const void *ctx, int motor)
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

static bool provider_pop_event(void *ctx, motor_event_t *out)
{
    return motor_pop_event((motor_executor_t *)ctx, out);
}

static bool provider_pop_event_for(void *ctx, int motor, motor_event_t *out)
{
    return motor_pop_event_for((motor_executor_t *)ctx, motor, out);
}

static const motor_exec_ops_t s_provider_ops = {
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

static motor_executor_t *executor_from_handle(motor_exec_t *exec)
{
    unsigned slot_id;

    for (slot_id = 0U; slot_id < WDF_MOTOR_EXECUTOR_INSTANCE_COUNT; ++slot_id) {
        if (s_slots[slot_id].bound && (&s_slots[slot_id].handle == exec)) {
            return &s_slots[slot_id].executor;
        }
    }
    return NULL;
}

static const motor_executor_t *const_executor_from_handle(const motor_exec_t *exec)
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
                                        motor_exec_t    **out_exec)
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
    motor_lock_init(&slot->executor);
    if (!slot->executor.lock_ready) {
        memset(slot, 0, sizeof(*slot));
        return init_err("lock init failed");
    }
    result = motor_init(&slot->executor, cfg, ports);
    if (!result.ok) {
        motor_lock_destroy(&slot->executor);
        memset(slot, 0, sizeof(*slot));
        return result;
    }
    if (!motor_exec_provider_bind(&slot->handle, &s_provider_ops, &slot->executor)) {
        motor_lock_destroy(&slot->executor);
        memset(slot, 0, sizeof(*slot));
        return init_err("provider binding failed");
    }
    slot->bound = true;
    *out_exec   = &slot->handle;
    return result;
}

motor_init_result_t motor_executor_reinit(motor_exec_t *exec)
{
    motor_executor_t *executor = executor_from_handle(exec);

    motor_init_result_t result;

    if (executor == NULL) {
        return init_err("executor unavailable");
    }
    motor_lock(executor);
    result = motor_reinit(executor);
    motor_unlock(executor);
    return result;
}

void motor_executor_tick(motor_exec_t *exec)
{
    motor_executor_t *executor = executor_from_handle(exec);

    if (executor != NULL) {
        motor_lock(executor);
        motor_tick(executor);
        motor_unlock(executor);
    }
}

motor_cmd_result_t motor_executor_zero_encoder(motor_exec_t *exec, int motor)
{
    motor_executor_t *executor = executor_from_handle(exec);

    return (executor != NULL) ? motor_zero_encoder(executor, motor) : cmd_reject(MOTOR_REJECT_UNAVAILABLE, "executor-unavailable");
}

motor_cmd_result_t motor_executor_confirm_baseline(motor_exec_t *exec, int motor)
{
    motor_executor_t *executor = executor_from_handle(exec);

    return (executor != NULL) ? motor_confirm_baseline(executor, motor) : cmd_reject(MOTOR_REJECT_UNAVAILABLE, "executor-unavailable");
}

void motor_executor_reset_watchdog(motor_exec_t *exec)
{
    motor_executor_t *executor = executor_from_handle(exec);

    if (executor != NULL) {
        motor_lock(executor);
        motor_reset_watchdog(executor);
        motor_unlock(executor);
    }
}

int motor_executor_current_freq(const motor_exec_t *exec, int motor)
{
    const motor_executor_t *executor = const_executor_from_handle(exec);

    int freq = 0;

    if (executor != NULL) {
        motor_lock((motor_executor_t *)executor);
        freq = motor_current_freq(executor, motor);
        motor_unlock((motor_executor_t *)executor);
    }
    return freq;
}

bool motor_executor_in_safe_state(const motor_exec_t *exec)
{
    const motor_executor_t *executor = const_executor_from_handle(exec);

    bool safe = false;

    if (executor != NULL) {
        motor_lock((motor_executor_t *)executor);
        safe = motor_in_safe_state(executor);
        motor_unlock((motor_executor_t *)executor);
    }
    return safe;
}

#ifdef MOTOR_EXECUTOR_UNIT_TEST
void motor_executor_test_reset(void)
{
    unsigned i;

    for (i = 0U; i < (unsigned)WDF_MOTOR_EXECUTOR_INSTANCE_COUNT; ++i) {
        motor_lock_destroy(&s_slots[i].executor);
    }
    memset(s_slots, 0, sizeof(s_slots));
}
#endif
