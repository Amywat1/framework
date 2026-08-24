/**
 * @file    motor_executor_cmd.c
 * @brief   电机执行器命令与查询
 * @author  huwangwei
 * @date    2026-08-21
 */

#include "domain/mechanism/motor/motor_executor_internal.h"
#include "domain/ports/outbound/safety/safety_output_hold.h"

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
    MOTOR_CMD_REJECT_SAFETY = 1u << 1  /**< 急停/看门狗锁定时拒绝 */
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
    if (bad_motor(e, i)) {
        return cmd_reject(MOTOR_REJECT_BAD_MOTOR, "bad-motor");
    }
    if (flags & MOTOR_CMD_NEED_NOW) {
        e->now = clock_now(e);
    }
    motor_leave_estop_if_unheld(e);
    if ((flags & MOTOR_CMD_REJECT_SAFETY) && (e->safe_latched || safety_output_hold_is_active())) {
        return cmd_reject(MOTOR_REJECT_SAFETY, "safety-locked");
    }
    return cmd_make(MOTOR_CMD_ACCEPTED, "");
}

/**
 * @brief  按配置判断该故障码是否需确认（调用方已保证电机号合法）
 * @note   `DRIVER_PORT_FATAL` 恒为需确认，与位图无关。
 */
static bool fault_code_requires_confirm_cfg(const motor_executor_t *e, int i, motor_exec_fault_code_t code)
{
    if (code == MOTOR_FAULT_DRIVER_PORT_FATAL) {
        return true;
    }
    if ((code <= MOTOR_FAULT_NONE) || ((unsigned)code > (unsigned)MOTOR_FAULT_SHARED_DRIVER)) {
        return true;
    }
    return (e->cfg.motors[i].confirm_faults & motor_fault_confirm_bit(code)) != 0u;
}

/**
 * @brief  单步故障恢复（调用方已持锁）
 */
static motor_cmd_result_t recover_one_step_locked(motor_executor_t *e, int i, motor_exec_recovery_step_t step)
{
    motor_mstate_t *s = &e->m[i];

    if (s->exec_state != MOTOR_STATE_FAULT) {
        return cmd_reject(MOTOR_REJECT_NOT_FAULT, "not-fault");
    }
    if (s->fatal) {
        return cmd_reject(MOTOR_REJECT_FATAL, "fatal-needs-reinit");
    }
    if (step == MOTOR_RECOVERY_DRIVER_RESET) {
        if (!drv_reset(motor_drv(e, i))) {
            push_event(e, i, MOTOR_EVENT_FAULT, MOTOR_END_NONE, s->fault_code);
            return cmd_reject(MOTOR_REJECT_DRIVER, "driver-reset-failed");
        }
        s->driver_reset_done = true;
        return cmd_make(MOTOR_CMD_ACCEPTED, "driver-reset");
    }
    if (!s->driver_reset_done) {
        return cmd_reject(MOTOR_REJECT_MUST_RESET, "must-reset-first");
    }
    e->now               = clock_now(e);
    s->exec_state        = MOTOR_STATE_STOPPED;
    s->fault_code        = MOTOR_FAULT_NONE;
    s->driver_reset_done = false;
    s->cooldown_until    = e->now;
    return cmd_make(MOTOR_CMD_ACCEPTED, "recovered");
}

/**
 * @brief  可续动非 fatal FAULT 在运动命令内完成两步 recover；否则按码拒令
 * @return 非 FAULT 或内清成功时 ACCEPTED；需确认 / fatal / 内清失败时为拒绝结果
 */
static motor_cmd_result_t try_clear_resumable_fault_locked(motor_executor_t *e, int i)
{
    motor_mstate_t    *s = &e->m[i];
    motor_cmd_result_t r;

    if (s->exec_state != MOTOR_STATE_FAULT) {
        return cmd_make(MOTOR_CMD_ACCEPTED, "");
    }
    if (s->fatal) {
        return cmd_reject(MOTOR_REJECT_FATAL, "fatal-needs-reinit");
    }
    if (fault_code_requires_confirm_cfg(e, i, s->fault_code)) {
        return cmd_reject(MOTOR_REJECT_FAULT, "fault");
    }
    r = recover_one_step_locked(e, i, MOTOR_RECOVERY_DRIVER_RESET);
    if (!motor_cmd_ok(r)) {
        return r;
    }
    return recover_one_step_locked(e, i, MOTOR_RECOVERY_MODULE_STOP);
}

motor_cmd_result_t motor_run(motor_executor_t        *e,
                             int                      i,
                             motor_speed_t            spd,
                             motor_dir_t              dir,
                             const motor_move_spec_t *spec)
{
    motor_cmd_result_t  g;
    motor_pending_cmd_t pc = {0};
    motor_cmd_result_t  out;

    motor_lock(e);
    g = cmd_guard(e, i, MOTOR_CMD_NEED_NOW | MOTOR_CMD_REJECT_SAFETY);
    if (!motor_cmd_ok(g)) {
        motor_unlock(e);
        return g;
    }
    if (!speed_valid(e, i, spd)) {
        motor_unlock(e);
        return cmd_reject(MOTOR_REJECT_BAD_SPEED, "bad-speed");
    }
    if (!motor_dir_is_motion(dir)) {
        motor_unlock(e);
        return cmd_reject(MOTOR_REJECT_BAD_DIR, "bad-dir");
    }
    if ((spec != NULL) && spec->use_position) {
        if (!e->cfg.motors[i].has_encoder) {
            motor_unlock(e);
            return cmd_reject(MOTOR_REJECT_NO_ENCODER, "no-encoder");
        }
        if (!e->m[i].baseline_trusted) {
            motor_unlock(e);
            return cmd_reject(MOTOR_REJECT_BASELINE, "baseline-untrusted");
        }
        if (!e->m[i].enc_healthy) {
            motor_unlock(e);
            return cmd_reject(MOTOR_REJECT_ENCODER, "encoder-unhealthy");
        }
    }
    /* 将进入 STOPPED 启动路径时先查互锁，避免可续动内清后因互锁拒令。 */
    if (((e->m[i].exec_state == MOTOR_STATE_STOPPED) || (e->m[i].exec_state == MOTOR_STATE_FAULT))
        && !interlock_ok(e, i)) {
        motor_unlock(e);
        return cmd_reject(MOTOR_REJECT_INTERLOCK, "interlock");
    }
    g = try_clear_resumable_fault_locked(e, i);
    if (!motor_cmd_ok(g)) {
        motor_unlock(e);
        return g;
    }
    pc.speed   = spd;
    pc.dir     = dir;
    pc.is_move = (spec != NULL);
    if (spec != NULL) {
        pc.spec = *spec;
    }
    out = apply_goal(e, i, &pc);
    motor_unlock(e);
    return out;
}

motor_cmd_result_t motor_stop(motor_executor_t *e, int i)
{
    motor_cmd_result_t g;
    motor_mstate_t    *s;
    motor_cmd_result_t out;

    motor_lock(e);
    g = cmd_guard(e, i, MOTOR_CMD_NEED_NOW);
    if (!motor_cmd_ok(g)) {
        motor_unlock(e);
        return g;
    }
    s = &e->m[i];
    switch (s->exec_state) {
    case MOTOR_STATE_WAITING_START:
        s->queued = false;
        if (s->output_applied) {
            settle_elapsed(e, i);
            s->emit_stop_on_halt = true;
            s->stop_issued       = false;
            s->exec_state             = MOTOR_STATE_STOPPING;
            out                  = cmd_make(MOTOR_CMD_ACCEPTED, "stopping");
            break;
        }
        s->exec_state = MOTOR_STATE_STOPPED;
        out      = cmd_make(MOTOR_CMD_ACCEPTED, "queue-cancel");
        break;
    case MOTOR_STATE_REVERSAL_WAIT:
        s->exec_state          = MOTOR_STATE_STOPPED;
        s->cooldown_until = e->now + e->cfg.motors[i].cooldown_ms;
        out               = cmd_make(MOTOR_CMD_ACCEPTED, "reversal-cancel");
        break;
    case MOTOR_STATE_RUNNING:
        settle_elapsed(e, i);
        s->queued            = false;
        s->emit_stop_on_halt = true;
        s->stop_issued       = false;
        s->exec_state             = MOTOR_STATE_STOPPING;
        out                  = cmd_make(MOTOR_CMD_ACCEPTED, "stopping");
        break;
    case MOTOR_STATE_STOPPING:
        s->queued            = false;
        s->emit_stop_on_halt = true;
        out                  = cmd_make(MOTOR_CMD_ACCEPTED, "stopping");
        break;
    default:
        out = cmd_make(MOTOR_CMD_ACCEPTED, "already-stopped");
        break;
    }
    motor_unlock(e);
    return out;
}

motor_cmd_result_t motor_home(motor_executor_t *e, int i)
{
    motor_cmd_result_t g;
    motor_move_spec_t  spec = {0};
    int                freq;
    motor_dir_t        dir;

    motor_lock(e);
    g = cmd_guard(e, i, MOTOR_CMD_NEED_NOW | MOTOR_CMD_REJECT_SAFETY);
    if (!motor_cmd_ok(g)) {
        motor_unlock(e);
        return g;
    }
    if (!e->cfg.motors[i].has_encoder) {
        motor_unlock(e);
        return cmd_reject(MOTOR_REJECT_NO_ENCODER, "no-encoder");
    }
    freq            = e->cfg.motors[i].slow_freq > 0 ? e->cfg.motors[i].slow_freq : MOTOR_HOME_DEFAULT_FREQ_CENTI_HZ;
    dir             = e->cfg.motors[i].home_dir;
    spec.limit_mask = MOTOR_LIMIT_MASK_ORIGIN;
    motor_unlock(e);
    return motor_run(e, i, motor_speed_freq(freq), dir, &spec);
}

motor_cmd_result_t motor_zero_encoder(motor_executor_t *e, int i)
{
    motor_cmd_result_t g;

    motor_lock(e);
    g = cmd_guard(e, i, 0);
    if (!motor_cmd_ok(g)) {
        motor_unlock(e);
        return g;
    }
    motor_encoder_t   *enc = motor_enc(e, i);
    motor_cmd_result_t out;

    if (!e->cfg.motors[i].has_encoder || !enc) {
        motor_unlock(e);
        return cmd_reject(MOTOR_REJECT_NO_ENCODER, "no-encoder");
    }
    if (e->cfg.motors[i].encoder_kind == MOTOR_ENC_ABSOLUTE) {
        motor_unlock(e);
        return cmd_reject(MOTOR_REJECT_ABSOLUTE, "absolute-encoder");
    }
    if (e->m[i].move_active && e->m[i].spec.use_position) {
        motor_unlock(e);
        return cmd_reject(MOTOR_REJECT_ACTIVE, "position-move-active");
    }
    e->now = clock_now(e);
    out    = cmd_reject(MOTOR_REJECT_ENCODER, "zero-failed");
    for (int t = 0; t < MOTOR_ZERO_MAX_TRIES; ++t) {
        if (enc_zero(enc)) {
            e->m[i].position = 0;
            e->m[i].last_raw = enc_raw(enc);
            out              = cmd_make(MOTOR_CMD_ACCEPTED, "zeroed");
            motor_unlock(e);
            return out;
        }
    }
    push_event(e, i, MOTOR_EVENT_WARNING, MOTOR_END_NONE, MOTOR_FAULT_ENCODER_SIGNAL);
    motor_unlock(e);
    return out;
}

motor_cmd_result_t motor_confirm_baseline(motor_executor_t *e, int i)
{
    motor_cmd_result_t out;

    motor_lock(e);
    if (bad_motor(e, i)) {
        motor_unlock(e);
        return cmd_reject(MOTOR_REJECT_BAD_MOTOR, "bad-motor");
    }
    e->m[i].baseline_trusted = true;
    out                      = cmd_make(MOTOR_CMD_ACCEPTED, "baseline-confirmed");
    motor_unlock(e);
    return out;
}

void motor_reset_watchdog(motor_executor_t *e)
{
    if (!e->safe_latched) {
        return;
    }
    e->safe_latched    = false;
    e->now             = clock_now(e);
    e->last_tick_ms    = e->now;
    e->last_tick_valid = true;
    for (int i = 0; i < e->motor_count; ++i) {
        if (e->m[i].fault_code == MOTOR_FAULT_WATCHDOG) {
            e->m[i].exec_state      = MOTOR_STATE_STOPPED;
            e->m[i].fault_code = MOTOR_FAULT_NONE;
        }
    }
}

motor_cmd_result_t motor_recover(motor_executor_t *e, int i, motor_exec_recovery_step_t step)
{
    motor_cmd_result_t g;
    motor_cmd_result_t out;

    motor_lock(e);
    g = cmd_guard(e, i, 0);
    if (!motor_cmd_ok(g)) {
        motor_unlock(e);
        return g;
    }

    out = recover_one_step_locked(e, i, step);
    motor_unlock(e);
    return out;
}

/* ------------------------- 查询 ------------------------- */

motor_exec_state_t motor_state(const motor_executor_t *e, int i)
{
    motor_exec_state_t exec_state;

    motor_lock((motor_executor_t *)e);
    if (bad_motor(e, i)) {
        motor_unlock((motor_executor_t *)e);
        return MOTOR_STATE_STOPPED;
    }
    exec_state = e->m[i].exec_state;
    motor_unlock((motor_executor_t *)e);
    return exec_state;
}

int64_t motor_position(const motor_executor_t *e, int i)
{
    int64_t pos;

    motor_lock((motor_executor_t *)e);
    if (bad_motor(e, i)) {
        motor_unlock((motor_executor_t *)e);
        return 0;
    }
    pos = e->m[i].position;
    motor_unlock((motor_executor_t *)e);
    return pos;
}

int motor_current_freq(const motor_executor_t *e, int i)
{
    const motor_mstate_t *state;
    int                   freq = 0;

    motor_lock((motor_executor_t *)e);
    if (bad_motor(e, i)) {
        motor_unlock((motor_executor_t *)e);
        return 0;
    }
    state = &e->m[i];
    if ((state->exec_state == MOTOR_STATE_RUNNING) && (state->speed.kind == MOTOR_SPEED_FREQ)) {
        freq = state->speed.value;
    }
    motor_unlock((motor_executor_t *)e);
    return freq;
}

motor_dir_t motor_direction(const motor_executor_t *e, int i)
{
    motor_dir_t dir;

    motor_lock((motor_executor_t *)e);
    if (bad_motor(e, i)) {
        motor_unlock((motor_executor_t *)e);
        return MOTOR_DIR_FORWARD;
    }
    dir = e->m[i].dir;
    motor_unlock((motor_executor_t *)e);
    return dir;
}

motor_exec_fault_code_t motor_fault_code(const motor_executor_t *e, int i)
{
    motor_exec_fault_code_t code;

    motor_lock((motor_executor_t *)e);
    if (bad_motor(e, i)) {
        motor_unlock((motor_executor_t *)e);
        return MOTOR_FAULT_NONE;
    }
    code = e->m[i].fault_code;
    motor_unlock((motor_executor_t *)e);
    return code;
}

bool motor_fault_requires_confirm(const motor_executor_t *e, int i, motor_exec_fault_code_t code)
{
    bool need;

    motor_lock((motor_executor_t *)e);
    if (bad_motor(e, i)) {
        motor_unlock((motor_executor_t *)e);
        return true;
    }
    need = fault_code_requires_confirm_cfg(e, i, code);
    motor_unlock((motor_executor_t *)e);
    return need;
}

bool motor_baseline_trusted(const motor_executor_t *e, int i)
{
    bool trusted;

    motor_lock((motor_executor_t *)e);
    if (bad_motor(e, i)) {
        motor_unlock((motor_executor_t *)e);
        return false;
    }
    /* 无编码器机构恒可信，与端口契约及 encoder_healthy 对称。 */
    if (!e->cfg.motors[i].has_encoder) {
        trusted = true;
    } else {
        trusted = e->m[i].baseline_trusted;
    }
    motor_unlock((motor_executor_t *)e);
    return trusted;
}

bool motor_encoder_healthy(const motor_executor_t *e, int i)
{
    bool healthy;

    motor_lock((motor_executor_t *)e);
    if (bad_motor(e, i)) {
        motor_unlock((motor_executor_t *)e);
        return false;
    }
    /* 无编码器的机构没有编码器可言，恒报健康，避免上层为它们维护无意义的条件。 */
    if (!e->cfg.motors[i].has_encoder) {
        healthy = true;
    } else {
        healthy = e->m[i].enc_healthy;
    }
    motor_unlock((motor_executor_t *)e);
    return healthy;
}

bool motor_in_safe_state(const motor_executor_t *e)
{
    bool safe;

    motor_lock((motor_executor_t *)e);
    safe = e->safe_latched;
    motor_unlock((motor_executor_t *)e);
    return safe;
}
