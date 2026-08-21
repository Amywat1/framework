/**
 * @file    motor_executor_cmd.c
 * @brief   电机执行器命令与查询
 * @author  huwangwei
 * @date    2026-08-21
 */

#include "domain/mechanism/motor/motor_executor_internal.h"

/* ------------------------- 速度解析 ------------------------- */

/**
 * @brief 校验速度指定，不改变控制类型和值
 */
static bool speed_valid(motor_executor_t *e, int i, motor_speed_t speed)
{
    if (speed.value <= 0) {
        return false;
    }
    if (speed.kind == MOTOR_SPEED_FREQ) {
        return true;
    }
    return (speed.kind == MOTOR_SPEED_GEAR) && (speed.value <= e->cfg.motors[i].gear_count);
}

/* ------------------------- 命令 ------------------------- */

enum {
    MOTOR_CMD_NEED_NOW      = 1u << 0, /**< 刷新 e->now */
    MOTOR_CMD_REJECT_SAFETY = 1u << 1, /**< 急停/看门狗锁定时拒绝 */
    MOTOR_CMD_REJECT_FAULT  = 1u << 2  /**< FAULT 相位时拒绝 */
};

static bool bad_motor(const motor_executor_t *e, int i)
{
    return i < 0 || i >= e->motor_count;
}

/**
 * @brief  命令公共前置校验
 * @return 通过时 status=ACCEPTED；失败时为拒绝结果
 */
static motor_cmd_result_t cmd_guard(motor_executor_t *e, int i, unsigned flags)
{
    if (e->in_dispatch) {
        return cmd_reject("reentrant");
    }
    if (bad_motor(e, i)) {
        return cmd_reject("bad-motor");
    }
    if (flags & MOTOR_CMD_NEED_NOW) {
        e->now = clock_now(e);
    }
    if ((flags & MOTOR_CMD_REJECT_SAFETY) && (e->estop_latched || e->safe_latched)) {
        return cmd_reject("safety-locked");
    }
    if ((flags & MOTOR_CMD_REJECT_FAULT) && (e->m[i].phase == MOTOR_PHASE_FAULT)) {
        return cmd_reject("fault");
    }
    return cmd_make(MOTOR_CMD_ACCEPTED, "");
}

motor_cmd_result_t motor_run(motor_executor_t            *e,
                                        int                          i,
                                        motor_speed_t            spd,
                                        motor_dir_t              dir,
                                        const motor_move_spec_t *spec)
{
    motor_cmd_result_t g  = cmd_guard(e, i, MOTOR_CMD_NEED_NOW | MOTOR_CMD_REJECT_SAFETY | MOTOR_CMD_REJECT_FAULT);
    motor_pending_cmd_t    pc = {0};

    if (!motor_cmd_ok(g)) {
        return g;
    }
    if (!speed_valid(e, i, spd)) {
        return cmd_reject("bad-speed");
    }
    if ((spec != NULL) && spec->use_position) {
        if (!e->cfg.motors[i].has_encoder) {
            return cmd_reject("no-encoder");
        }
        if (!e->m[i].baseline_trusted) {
            return cmd_reject("baseline-untrusted");
        }
        /* 降级策略：编码器异常后位置读数不可信，拒绝按位置运动，避免依据错误位置
         * 动作造成碰撞。限位运动与归位仍放行，机器据此可自行走回原点恢复。 */
        if (!e->m[i].enc_healthy) {
            return cmd_reject("encoder-unhealthy");
        }
    }
    pc.speed   = spd;
    pc.dir     = dir;
    pc.is_move = (spec != NULL);
    if (spec != NULL) {
        pc.spec = *spec;
    }
    return apply_goal(e, i, &pc);
}

motor_cmd_result_t motor_stop(motor_executor_t *e, int i)
{
    motor_cmd_result_t g = cmd_guard(e, i, MOTOR_CMD_NEED_NOW);

    if (!motor_cmd_ok(g)) {
        return g;
    }
    motor_mstate_t *s = &e->m[i];
    switch (s->phase) {
    case MOTOR_PHASE_WAITING_START:
        s->queued = false;
        s->phase  = MOTOR_PHASE_STOPPED;
        return cmd_make(MOTOR_CMD_ACCEPTED, "queue-cancel");
    case MOTOR_PHASE_REVERSAL_WAIT:
        s->phase          = MOTOR_PHASE_STOPPED;
        s->cooldown_until = e->now + e->cfg.motors[i].cooldown_ms;
        return cmd_make(MOTOR_CMD_ACCEPTED, "reversal-cancel");
    case MOTOR_PHASE_RUNNING:
    case MOTOR_PHASE_DECELERATING: {
        /* 上位 stop 与触限同拍：本运动已压在监视硬限位上，按到位收尾，
         * 避免清 move_active 后丢掉 ARRIVED，只剩 STOPPED/END_NONE。 */
        static const motor_limit_kind_t k_order[] = {
            MOTOR_LIMIT_ORIGIN,
            MOTOR_LIMIT_POS,
            MOTOR_LIMIT_NEG,
        };
        unsigned n;

        for (n = 0U; n < (sizeof(k_order) / sizeof(k_order[0])); ++n) {
            motor_limit_kind_t kind = k_order[n];

            if (motor_limit_mask_has(s->spec.limit_mask, kind) && sensor_limit(e, i, kind)) {
                s->end_limit = kind;
                complete_move(e, i, MOTOR_EVENT_ARRIVED, MOTOR_END_LIMIT);
                /* 命令路径不经 tick，立即派发到位事件。 */
                motor_dispatch(e);
                return cmd_make(MOTOR_CMD_ACCEPTED, "arrived-on-limit");
            }
        }
        /* 先结算已运行时长，转入减速后计时起点失效，随后的停止事件才有耗时。 */
        settle_elapsed(e, i);
        s->emit_stop_on_halt = true;
        s->move_active       = false;
        s->phase             = MOTOR_PHASE_DECELERATING;
        return cmd_make(MOTOR_CMD_ACCEPTED, "stopping");
    }
    default:
        return cmd_make(MOTOR_CMD_ACCEPTED, "already-stopped");
    }
}

motor_cmd_result_t motor_home(motor_executor_t *e, int i)
{
    motor_cmd_result_t g = cmd_guard(e, i, MOTOR_CMD_NEED_NOW | MOTOR_CMD_REJECT_SAFETY | MOTOR_CMD_REJECT_FAULT);
    motor_move_spec_t  spec = {0};
    int                    freq;

    if (!motor_cmd_ok(g)) {
        return g;
    }
    if (!e->cfg.motors[i].has_encoder) {
        return cmd_reject("no-encoder");
    }
    freq            = e->cfg.motors[i].slow_freq > 0 ? e->cfg.motors[i].slow_freq : MOTOR_HOME_DEFAULT_FREQ_CENTI_HZ;
    spec.limit_mask = MOTOR_LIMIT_MASK_ORIGIN;
    return motor_run(e, i, motor_speed_freq(freq), MOTOR_DIR_REVERSE, &spec);
}

motor_cmd_result_t motor_zero_encoder(motor_executor_t *e, int i)
{
    motor_cmd_result_t g = cmd_guard(e, i, 0);

    if (!motor_cmd_ok(g)) {
        return g;
    }
    motor_encoder_t *enc = motor_enc(e, i);
    if (!e->cfg.motors[i].has_encoder || !enc) {
        return cmd_reject("no-encoder");
    }
    if (e->cfg.motors[i].encoder_kind == MOTOR_ENC_ABSOLUTE) {
        return cmd_reject("absolute-encoder");
    }
    if (e->m[i].move_active && e->m[i].spec.use_position) {
        return cmd_reject("position-move-active");
    }
    e->now = clock_now(e);
    for (int t = 0; t < MOTOR_ZERO_MAX_TRIES; ++t) {
        if (enc_zero(enc)) {
            e->m[i].position = 0;
            e->m[i].last_raw = enc_raw(enc);
            return cmd_make(MOTOR_CMD_ACCEPTED, "zeroed");
        }
    }
    push_event(e, i, MOTOR_EVENT_WARNING, MOTOR_END_NONE, MOTOR_FAULT_ENCODER_SIGNAL);
    return cmd_reject("zero-failed");
}

motor_cmd_result_t motor_confirm_baseline(motor_executor_t *e, int i)
{
    if (bad_motor(e, i)) {
        return cmd_reject("bad-motor");
    }
    e->m[i].baseline_trusted = true;
    return cmd_make(MOTOR_CMD_ACCEPTED, "baseline-confirmed");
}

void motor_reset_estop(motor_executor_t *e)
{
    if (e->in_dispatch) {
        return;
    }
    if (!e->estop_latched) {
        return;
    }
    if (estop_active(e)) {
        return; /* 未解除不可复位 */
    }
    e->estop_latched = false;
    for (int i = 0; i < e->motor_count; ++i) {
        if (e->m[i].phase == MOTOR_PHASE_ESTOP) {
            e->m[i].phase = MOTOR_PHASE_STOPPED;
        }
    }
    e->now = clock_now(e);
    for (int i = 0; i < e->motor_count; ++i) {
        e->m[i].cooldown_until = e->now;
    }
}

void motor_reset_watchdog(motor_executor_t *e)
{
    if (e->in_dispatch) {
        return;
    }
    if (!e->safe_latched) {
        return;
    }
    e->safe_latched    = false;
    e->now             = clock_now(e);
    e->last_tick_ms    = e->now;
    e->last_tick_valid = true;
    for (int i = 0; i < e->motor_count; ++i) {
        if (e->m[i].fault_code == MOTOR_FAULT_WATCHDOG) {
            e->m[i].phase      = MOTOR_PHASE_STOPPED;
            e->m[i].fault_code = MOTOR_FAULT_NONE;
        }
    }
}

motor_cmd_result_t motor_recover(motor_executor_t *e, int i, motor_exec_recovery_step_t step)
{
    motor_cmd_result_t g = cmd_guard(e, i, 0);

    if (!motor_cmd_ok(g)) {
        return g;
    }
    motor_mstate_t *s = &e->m[i];
    if (s->phase != MOTOR_PHASE_FAULT) {
        return cmd_reject("not-fault");
    }
    if (s->fatal) {
        return cmd_reject("fatal-needs-reinit");
    }
    if (step == MOTOR_RECOVERY_DRIVER_RESET) {
        if (!drv_reset(motor_drv(e, i))) {
            push_event(e, i, MOTOR_EVENT_FAULT, MOTOR_END_NONE, s->fault_code);
            return cmd_reject("driver-reset-failed");
        }
        s->driver_reset_done = true;
        return cmd_make(MOTOR_CMD_ACCEPTED, "driver-reset");
    }
    /* MODULE_STOP */
    if (!s->driver_reset_done) {
        return cmd_reject("must-reset-first");
    }
    e->now               = clock_now(e);
    s->phase             = MOTOR_PHASE_STOPPED;
    s->fault_code        = MOTOR_FAULT_NONE;
    s->driver_reset_done = false;
    s->cooldown_until    = e->now;
    return cmd_make(MOTOR_CMD_ACCEPTED, "recovered");
}

/* ------------------------- 查询 ------------------------- */

motor_exec_phase_t motor_phase(const motor_executor_t *e, int i)
{
    if (bad_motor(e, i)) {
        return MOTOR_PHASE_STOPPED;
    }
    return e->m[i].phase;
}

int64_t motor_position(const motor_executor_t *e, int i)
{
    if (bad_motor(e, i)) {
        return 0;
    }
    return e->m[i].position;
}

int motor_current_freq(const motor_executor_t *e, int i)
{
    const motor_mstate_t *state;

    if (bad_motor(e, i)) {
        return 0;
    }
    state = &e->m[i];

    if ((state->phase != MOTOR_PHASE_RUNNING) || (state->speed.kind != MOTOR_SPEED_FREQ)) {
        return 0;
    }
    return state->speed.value;
}

motor_dir_t motor_direction(const motor_executor_t *e, int i)
{
    if (bad_motor(e, i)) {
        return MOTOR_DIR_FORWARD;
    }
    return e->m[i].dir;
}

motor_exec_fault_code_t motor_fault_code(const motor_executor_t *e, int i)
{
    if (bad_motor(e, i)) {
        return MOTOR_FAULT_NONE;
    }
    return e->m[i].fault_code;
}

bool motor_baseline_trusted(const motor_executor_t *e, int i)
{
    if (bad_motor(e, i)) {
        return false;
    }
    /* 无编码器机构恒可信，与端口契约及 encoder_healthy 对称。 */
    if (!e->cfg.motors[i].has_encoder) {
        return true;
    }
    return e->m[i].baseline_trusted;
}

bool motor_encoder_healthy(const motor_executor_t *e, int i)
{
    if (bad_motor(e, i)) {
        return false;
    }
    /* 无编码器的机构没有编码器可言，恒报健康，避免上层为它们维护无意义的条件。 */
    if (!e->cfg.motors[i].has_encoder) {
        return true;
    }
    return e->m[i].enc_healthy;
}

bool motor_in_safe_state(const motor_executor_t *e)
{
    return e->safe_latched;
}
