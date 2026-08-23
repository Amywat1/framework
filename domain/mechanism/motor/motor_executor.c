/**
 * @file    motor_executor.c
 * @brief   电机执行器组合件：事件队列、槽池与出站端口实现
 *
 * 采用显式状态机驱动，逐 tick 推进。除急停外命令均为异步语义。
 * 关键控制逻辑（启动/换向/停止/故障/急停/看门狗）均以状态迁移表达，
 * 上电默认态与故障安全态均为“停止且输出关断”。
 * motor_exec_* 由本文件直接实现；句柄内嵌执行器，不经 ops 表分派。
 */
#include "domain/mechanism/motor/motor_executor.h"

#include "domain/mechanism/motor/motor_executor_internal.h"

#include <string.h>

/** @brief 出站端口句柄的完整类型；对外仍是不完整类型。 */
struct motor_exec {
    motor_executor_t impl;
};

typedef struct {
    bool         bound;
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
    if (s->exec_state == MOTOR_STATE_RUNNING) {
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
    if (s->exec_state == MOTOR_STATE_RUNNING) {
        s->elapsed_ms += e->now - s->move_start_ms;
        s->move_start_ms = e->now;
    }
}

void push_event(motor_executor_t       *e,
                int                     i,
                motor_event_type_t      t,
                motor_end_condition_t   trig,
                motor_exec_fault_code_t fc)
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

/* ------------------------- 槽池生命周期 ------------------------- */

static motor_executor_t *executor_from_handle(motor_exec_t *exec)
{
    unsigned slot_id;

    for (slot_id = 0U; slot_id < WDF_MOTOR_EXECUTOR_INSTANCE_COUNT; ++slot_id) {
        if (s_slots[slot_id].bound && (&s_slots[slot_id].handle == exec)) {
            return &exec->impl;
        }
    }
    return NULL;
}

static const motor_executor_t *const_executor_from_handle(const motor_exec_t *exec)
{
    unsigned slot_id;

    for (slot_id = 0U; slot_id < WDF_MOTOR_EXECUTOR_INSTANCE_COUNT; ++slot_id) {
        if (s_slots[slot_id].bound && (&s_slots[slot_id].handle == exec)) {
            return &exec->impl;
        }
    }
    return NULL;
}

/* ------------------------- 出站端口 ------------------------- */

motor_cmd_result_t motor_exec_run(motor_exec_t            *exec,
                                  int                      motor,
                                  motor_speed_t            speed,
                                  motor_dir_t              dir,
                                  const motor_move_spec_t *spec)
{
    motor_executor_t *executor = executor_from_handle(exec);

    return (executor != NULL) ? motor_run(executor, motor, speed, dir, spec)
                              : cmd_reject(MOTOR_REJECT_UNAVAILABLE, "executor-unavailable");
}

motor_cmd_result_t motor_exec_stop(motor_exec_t *exec, int motor)
{
    motor_executor_t *executor = executor_from_handle(exec);

    return (executor != NULL) ? motor_stop(executor, motor)
                              : cmd_reject(MOTOR_REJECT_UNAVAILABLE, "executor-unavailable");
}

motor_cmd_result_t motor_exec_home(motor_exec_t *exec, int motor)
{
    motor_executor_t *executor = executor_from_handle(exec);

    return (executor != NULL) ? motor_home(executor, motor)
                              : cmd_reject(MOTOR_REJECT_UNAVAILABLE, "executor-unavailable");
}

motor_cmd_result_t motor_exec_recover(motor_exec_t *exec, int motor, motor_exec_recovery_step_t step)
{
    motor_executor_t *executor = executor_from_handle(exec);

    return (executor != NULL) ? motor_recover(executor, motor, step)
                              : cmd_reject(MOTOR_REJECT_UNAVAILABLE, "executor-unavailable");
}

motor_exec_state_t motor_exec_state(const motor_exec_t *exec, int motor)
{
    const motor_executor_t *executor = const_executor_from_handle(exec);

    return (executor != NULL) ? motor_state(executor, motor) : MOTOR_STATE_STOPPED;
}

int64_t motor_exec_position(const motor_exec_t *exec, int motor)
{
    const motor_executor_t *executor = const_executor_from_handle(exec);

    return (executor != NULL) ? motor_position(executor, motor) : 0;
}

motor_dir_t motor_exec_direction(const motor_exec_t *exec, int motor)
{
    const motor_executor_t *executor = const_executor_from_handle(exec);

    return (executor != NULL) ? motor_direction(executor, motor) : MOTOR_DIR_FORWARD;
}

motor_exec_fault_code_t motor_exec_fault_code(const motor_exec_t *exec, int motor)
{
    const motor_executor_t *executor = const_executor_from_handle(exec);

    return (executor != NULL) ? motor_fault_code(executor, motor) : MOTOR_FAULT_NONE;
}

bool motor_exec_encoder_healthy(const motor_exec_t *exec, int motor)
{
    const motor_executor_t *executor = const_executor_from_handle(exec);

    return (executor != NULL) && motor_encoder_healthy(executor, motor);
}

bool motor_exec_baseline_trusted(const motor_exec_t *exec, int motor)
{
    const motor_executor_t *executor = const_executor_from_handle(exec);

    return (executor != NULL) && motor_baseline_trusted(executor, motor);
}

bool motor_exec_pop_event(motor_exec_t *exec, motor_event_t *out)
{
    motor_executor_t *executor = executor_from_handle(exec);

    return (executor != NULL) && (out != NULL) && motor_pop_event(executor, out);
}

bool motor_exec_pop_event_for(motor_exec_t *exec, int motor, motor_event_t *out)
{
    motor_executor_t *executor = executor_from_handle(exec);

    return (executor != NULL) && (out != NULL) && motor_pop_event_for(executor, motor, out);
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
                                        motor_exec_t        **out_exec)
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
    motor_lock_init(&slot->handle.impl);
    if (!slot->handle.impl.lock_ready) {
        memset(slot, 0, sizeof(*slot));
        return init_err("lock init failed");
    }
    result = motor_init(&slot->handle.impl, cfg, ports);
    if (!result.ok) {
        motor_lock_destroy(&slot->handle.impl);
        memset(slot, 0, sizeof(*slot));
        return result;
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

    return (executor != NULL) ? motor_zero_encoder(executor, motor)
                              : cmd_reject(MOTOR_REJECT_UNAVAILABLE, "executor-unavailable");
}

motor_cmd_result_t motor_executor_confirm_baseline(motor_exec_t *exec, int motor)
{
    motor_executor_t *executor = executor_from_handle(exec);

    return (executor != NULL) ? motor_confirm_baseline(executor, motor)
                              : cmd_reject(MOTOR_REJECT_UNAVAILABLE, "executor-unavailable");
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
        motor_lock_destroy(&s_slots[i].handle.impl);
    }
    memset(s_slots, 0, sizeof(s_slots));
}
#endif
