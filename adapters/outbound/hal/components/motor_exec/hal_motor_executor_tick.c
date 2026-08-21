/**
 * @file    hal_motor_executor_tick.c
 * @brief   电机执行器 tick 与相位状态机
 * @author  huwangwei
 * @date    2026-08-21
 */

#include "adapters/outbound/hal/components/motor_exec/hal_motor_executor_internal.h"

/* ------------------------- 输出 ------------------------- */

static bool speed_equal(hal_motor_speed_t left, hal_motor_speed_t right)
{
    return (left.kind == right.kind) && (left.value == right.value);
}

static hal_motor_speed_t desired_output_speed(motor_executor_t *e, int i)
{
    motor_mstate_t          *state = &e->m[i];
    const motor_motor_cfg_t *cfg   = &e->cfg.motors[i];
    hal_motor_speed_t        speed = state->speed;

    if (state->move_active && state->spec.use_position && (speed.kind == HAL_MOTOR_SPEED_GEAR) && (cfg->decel_point > 0)
        && (cfg->position_slow_gear > 0) && within_distance(state->spec.target_pos, state->position, cfg->decel_point)
        && (speed.value > cfg->position_slow_gear)) {
        speed.value = cfg->position_slow_gear;
    }
    return speed;
}

static bool apply_output(motor_executor_t *e, int i)
{
    motor_mstate_t   *s       = &e->m[i];
    hal_motor_speed_t desired = desired_output_speed(e, i);

    if (s->output_applied && speed_equal(s->applied_speed, desired)) {
        return true;
    }
    if (drv_set_output(motor_drv(e, i), desired, s->dir) != SW_OK) {
        return false;
    }
    s->applied_speed  = desired;
    s->output_applied = true;
    return true;
}

static bool immediate_cut(motor_executor_t *e, int i)
{
    e->m[i].output_applied = false;
    return drv_cutoff(motor_drv(e, i)) == SW_OK;
}

/* ------------------------- 前向声明 ------------------------- */
static void fault_one(motor_executor_t *e, int i, hal_motor_fault_code_t code, bool fatal);
static void link_shared_driver(motor_executor_t *e, int i);
static void enter_fault(motor_executor_t *e, int i, hal_motor_fault_code_t code);
static void begin_start(motor_executor_t *e, int i, const motor_pending_cmd_t *pc);

/* ------------------------- 互锁 ------------------------- */

static bool interlock_ok(motor_executor_t *e, int i)
{
    for (int k = 0; k < e->cfg.interlock_count; ++k) {
        const motor_interlock_t *il = &e->cfg.interlocks[k];
        if (il->a != i) {
            continue;
        }
        if (il->kind == MOTOR_INTERLOCK_MUTEX) {
            hal_motor_phase_t bp = e->m[il->b].phase;
            if (bp == HAL_MOTOR_PHASE_RUNNING || bp == HAL_MOTOR_PHASE_DECELERATING
                || bp == HAL_MOTOR_PHASE_WAITING_START || bp == HAL_MOTOR_PHASE_REVERSAL_WAIT) {
                return false;
            }
        } else {
            if (!e->m[il->b].baseline_trusted) {
                return false;
            }
            int64_t pos = e->m[il->b].position;
            if (pos < il->pos_min || pos > il->pos_max) {
                return false;
            }
        }
    }
    return true;
}

/* ------------------------- 启动 ------------------------- */

/**
 * @brief 尝试完成预备并进入 RUNNING；BUSY 时留在 WAITING_START 等待下一拍。
 */
static void begin_start(motor_executor_t *e, int i, const motor_pending_cmd_t *pc)
{
    motor_mstate_t          *s  = &e->m[i];
    const motor_motor_cfg_t *mc = &e->cfg.motors[i];

    if (mc->prep_required) {
        motor_prepare_result_t prep = drv_prepare(motor_drv(e, i), i);

        if (prep == MOTOR_PREPARE_BUSY) {
            s->pending = *pc;
            s->queued  = true;
            s->phase   = HAL_MOTOR_PHASE_WAITING_START;
            return;
        }
        if (prep == MOTOR_PREPARE_FAILED) {
            fault_one(e, i, HAL_MOTOR_FAULT_PREPARE_FAILED, false);
            push_event(e, i, HAL_MOTOR_EVENT_FAULT, HAL_MOTOR_END_NONE, HAL_MOTOR_FAULT_PREPARE_FAILED);
            link_shared_driver(e, i);
            return;
        }
    }

    s->dir               = pc->dir;
    s->speed             = pc->speed;
    s->output_applied    = false;
    s->move_active       = pc->is_move;
    s->spec              = pc->spec;
    s->end_limit         = HAL_MOTOR_LIMIT_POS;
    s->move_start_ms     = e->now;
    s->elapsed_ms        = 0;
    s->start_ms          = e->now;
    s->emit_stop_on_halt = false;
    s->cur_over_ms       = 0;
    s->cur_under_ms      = 0;
    s->cur_stop_ms       = 0;
    s->fb_bad_ms         = 0;
    s->temp_bad_ms       = 0;
    s->volt_bad_ms       = 0;
    s->fb_strikes        = 0;
    s->enc_stall         = 0;
    s->enc_warned        = false;
    s->queued            = false;
    s->phase             = HAL_MOTOR_PHASE_RUNNING;
}

static bool reversal(motor_executor_t *e, int i, const motor_pending_cmd_t *pc)
{
    motor_mstate_t *s = &e->m[i];

    if (!immediate_cut(e, i)) {
        enter_fault(e, i, HAL_MOTOR_FAULT_DRIVER_PORT_FATAL);
        return false;
    }
    s->after_reversal = *pc;
    s->reversal_until = e->now + e->cfg.motors[i].reversal_stop_ms;
    s->phase          = HAL_MOTOR_PHASE_REVERSAL_WAIT;
    return true;
}

static bool in_cooldown(motor_executor_t *e, int i)
{
    return e->now < e->m[i].cooldown_until;
}

/**
 * @brief  输出已建立且方向/速度种类不变：只更新目标，不重开输出会话
 */
static void apply_goal_in_place(motor_mstate_t *s, const motor_pending_cmd_t *pc)
{
    if (!speed_equal(s->speed, pc->speed)) {
        s->output_applied = false;
    }
    s->dir               = pc->dir;
    s->speed             = pc->speed;
    s->move_active       = pc->is_move;
    s->spec              = pc->spec;
    s->emit_stop_on_halt = false;
}

static hal_motor_cmd_result_t after_begin_start(motor_executor_t *e, int i, const char *started_reason)
{
    hal_motor_phase_t phase = e->m[i].phase;

    if (phase == HAL_MOTOR_PHASE_WAITING_START) {
        return cmd_make(HAL_MOTOR_CMD_QUEUED, "prepare");
    }
    if (phase == HAL_MOTOR_PHASE_FAULT) {
        return cmd_make(HAL_MOTOR_CMD_ACCEPTED, "prepare-failed");
    }
    return cmd_make(HAL_MOTOR_CMD_ACCEPTED, started_reason);
}

static hal_motor_cmd_result_t enter_reversal(motor_executor_t          *e,
                                             int                        i,
                                             const motor_pending_cmd_t *pc,
                                             const char                *reason)
{
    if (!reversal(e, i, pc)) {
        return cmd_make(HAL_MOTOR_CMD_ACCEPTED, "cutoff-failed");
    }
    return cmd_make(HAL_MOTOR_CMD_ACCEPTED, reason);
}

/**
 * @brief  锁存运动目标并由内部状态机收敛；调用方不必按相位选命令
 */
hal_motor_cmd_result_t apply_goal(motor_executor_t *e, int i, const motor_pending_cmd_t *pc)
{
    motor_mstate_t *s = &e->m[i];

    switch (s->phase) {
    case HAL_MOTOR_PHASE_RUNNING:
        if ((pc->dir != s->dir) || (pc->speed.kind != s->speed.kind)) {
            return enter_reversal(e, i, pc, "reversal");
        }
        apply_goal_in_place(s, pc);
        return cmd_make(HAL_MOTOR_CMD_ACCEPTED, "goal-updated");

    case HAL_MOTOR_PHASE_DECELERATING:
        if ((pc->dir != s->dir) || (pc->speed.kind != s->speed.kind)) {
            return enter_reversal(e, i, pc, "output-mode-change");
        }
        begin_start(e, i, pc);
        return after_begin_start(e, i, "restart");

    case HAL_MOTOR_PHASE_WAITING_START:
        s->pending = *pc;
        s->queued  = true;
        return cmd_make(HAL_MOTOR_CMD_QUEUED, "pending-updated");

    case HAL_MOTOR_PHASE_REVERSAL_WAIT:
        s->after_reversal = *pc;
        return cmd_make(HAL_MOTOR_CMD_QUEUED, "reversal-goal-updated");

    case HAL_MOTOR_PHASE_STOPPED:
        if (!interlock_ok(e, i)) {
            return cmd_reject("interlock");
        }
        if (in_cooldown(e, i)) {
            s->pending = *pc;
            s->queued  = true;
            s->phase   = HAL_MOTOR_PHASE_WAITING_START;
            return cmd_make(HAL_MOTOR_CMD_QUEUED, "cooldown");
        }
        begin_start(e, i, pc);
        return after_begin_start(e, i, "start");

    default:
        return cmd_reject("bad-phase");
    }
}

/* ------------------------- 故障 ------------------------- */

static void fault_one(motor_executor_t *e, int i, hal_motor_fault_code_t code, bool fatal)
{
    motor_mstate_t *s = &e->m[i];
    (void)immediate_cut(e, i);
    /* 结算耗时后再转入故障态，故障事件才能带上已运行时长。 */
    settle_elapsed(e, i);
    s->phase             = HAL_MOTOR_PHASE_FAULT;
    s->fatal             = fatal;
    s->fault_code        = code;
    s->move_active       = false;
    s->queued            = false;
    s->driver_reset_done = false;
}

static void link_shared_driver(motor_executor_t *e, int i)
{
    int di = e->cfg.motors[i].driver_index;
    for (int j = 0; j < e->motor_count; ++j) {
        if (j == i) {
            continue;
        }
        if (e->cfg.motors[j].driver_index != di) {
            continue;
        }
        if (e->m[j].phase == HAL_MOTOR_PHASE_FAULT || e->m[j].phase == HAL_MOTOR_PHASE_ESTOP) {
            continue;
        }
        fault_one(e, j, HAL_MOTOR_FAULT_SHARED_DRIVER, false);
        push_event(e, j, HAL_MOTOR_EVENT_FAULT, HAL_MOTOR_END_NONE, HAL_MOTOR_FAULT_SHARED_DRIVER);
    }
}

static void enter_fault(motor_executor_t *e, int i, hal_motor_fault_code_t code)
{
    fault_one(e, i, code, false);
    push_event(e, i, HAL_MOTOR_EVENT_FAULT, HAL_MOTOR_END_NONE, code);
    link_shared_driver(e, i);
}

static void enter_fatal(motor_executor_t *e, int i, hal_motor_fault_code_t code)
{
    fault_one(e, i, code, true);
    push_event(e, i, HAL_MOTOR_EVENT_FAULT, HAL_MOTOR_END_NONE, code);
}

static void warn(motor_executor_t *e, int i, hal_motor_fault_code_t code)
{
    if (e->cfg.motors[i].enc_escalate && code == HAL_MOTOR_FAULT_ENCODER_SIGNAL) {
        enter_fault(e, i, code);
        return;
    }
    push_event(e, i, HAL_MOTOR_EVENT_WARNING, HAL_MOTOR_END_NONE, code);
}

/* ------------------------- 停止 / 到位 ------------------------- */

static void finish_halt(motor_executor_t *e, int i)
{
    motor_mstate_t *s = &e->m[i];
    if (!immediate_cut(e, i)) {
        enter_fault(e, i, HAL_MOTOR_FAULT_DRIVER_PORT_FATAL);
        return;
    }
    /* 置 STOPPED 前先结算耗时，否则随后的停止事件只能读到未累加的 0。 */
    settle_elapsed(e, i);
    /* ORIGIN 限位运动被外部停止时，只要机构确实压在原点就必须重建基准：清零依据是
     * 机构位置，与运动因何结束、是否经 motor_home 无关。上层可能与本执行器同拍
     * 看到原点限位并先下发停止，此时走 finish_halt 而非 complete_move。 */
    if (hal_motor_limit_mask_has(s->spec.limit_mask, HAL_MOTOR_LIMIT_ORIGIN)
        && sensor_limit(e, i, HAL_MOTOR_LIMIT_ORIGIN)) {
        (void)zero_encoder_baseline(e, i);
    }
    s->phase          = HAL_MOTOR_PHASE_STOPPED;
    s->move_active    = false;
    s->cooldown_until = e->now + e->cfg.motors[i].cooldown_ms;
    if (s->emit_stop_on_halt) {
        s->emit_stop_on_halt = false;
        push_event(e, i, HAL_MOTOR_EVENT_STOPPED, HAL_MOTOR_END_NONE, HAL_MOTOR_FAULT_NONE);
    }
}

/**
 * @brief  到达原点后建立可信基准（有编码器时清零）
 * @note   无编码器轴无位置基准概念，直接标可信。
 * @note   绝对编码器：限位只作停机，不改写 position（真值始终来自传感器采样）。
 */
bool zero_encoder_baseline(motor_executor_t *e, int i)
{
    motor_mstate_t  *s = &e->m[i];
    motor_encoder_t *enc;

    if (!e->cfg.motors[i].has_encoder) {
        s->baseline_trusted = true;
        return true;
    }
    if (e->cfg.motors[i].encoder_kind == MOTOR_ENC_ABSOLUTE) {
        /* 绝对轴：触限位不改位置，仅标记基准可信 */
        s->baseline_trusted = true;
        s->enc_healthy      = true;
        return true;
    }
    enc = motor_enc(e, i);
    if (!enc) {
        s->baseline_trusted = false;
        return false;
    }
    s->baseline_trusted = false;
    for (int attempt = 0; attempt < MOTOR_ZERO_MAX_TRIES; ++attempt) {
        if (enc_zero(enc)) {
            s->position         = 0;
            s->last_raw         = enc_raw(enc);
            s->baseline_trusted = true;
            /* 归位成功重建基准，编码器读数重新可信：这是异常后的唯一恢复路径。 */
            s->enc_healthy = true;
            return true;
        }
    }
    return false;
}

void complete_move(motor_executor_t *e, int i, hal_motor_event_type_t type, hal_motor_end_condition_t trig)
{
    motor_mstate_t *s = &e->m[i];
    /* 清零依据是「监视 ORIGIN 且结束时机构确实压在原点」，与是否 motor_home、
     * 运动因何结束无关。上层可能同拍先下发停止（END_NONE），仍须重建基准。 */
    bool origin_reached = hal_motor_limit_mask_has(s->spec.limit_mask, HAL_MOTOR_LIMIT_ORIGIN)
                          && sensor_limit(e, i, HAL_MOTOR_LIMIT_ORIGIN);

    /* 先结算耗时，保留到位瞬间的时长供随后的事件读取。 */
    settle_elapsed(e, i);
    s->move_active       = false;
    s->emit_stop_on_halt = false;
    if (origin_reached) {
        finish_halt(e, i);
        if (s->phase == HAL_MOTOR_PHASE_FAULT) {
            return;
        }
        /* finish_halt 已尝试建基准；失败则进入故障（与旧 complete_move 路径一致）。 */
        if (!s->baseline_trusted) {
            enter_fault(e, i, HAL_MOTOR_FAULT_ENCODER_SIGNAL);
            return;
        }
        push_event(e, i, type, trig, HAL_MOTOR_FAULT_NONE);
        return;
    }

    /* 非原点动作保留到位瞬间的位置和耗时。 */
    push_event(e, i, type, trig, HAL_MOTOR_FAULT_NONE);
    finish_halt(e, i);
}

static void check_end(motor_executor_t *e, int i)
{
    motor_mstate_t          *s  = &e->m[i];
    const motor_motor_cfg_t *mc = &e->cfg.motors[i];
    if (!s->move_active) {
        return;
    }
    /* 优先级：硬限位（mask OR）→ 位置 → 软限位 → 电流 → 时间 → 超时兜底
     * 硬限位同拍多路：ORIGIN → POS → NEG */
    if (s->spec.limit_mask != 0u) {
        static const hal_motor_limit_kind_t k_order[] = {
            HAL_MOTOR_LIMIT_ORIGIN,
            HAL_MOTOR_LIMIT_POS,
            HAL_MOTOR_LIMIT_NEG,
        };
        unsigned n;

        for (n = 0; n < (sizeof(k_order) / sizeof(k_order[0])); ++n) {
            hal_motor_limit_kind_t kind = k_order[n];
            if (hal_motor_limit_mask_has(s->spec.limit_mask, kind) && sensor_limit(e, i, kind)) {
                s->end_limit = kind;
                complete_move(e, i, HAL_MOTOR_EVENT_ARRIVED, HAL_MOTOR_END_LIMIT);
                return;
            }
        }
    }
    if (s->spec.use_position) {
        int64_t target    = s->spec.target_pos;
        int     tolerance = mc->pos_tolerance;
        bool    reached   = (s->dir == HAL_MOTOR_DIR_FORWARD) ? (s->position >= lower_bound(target, tolerance))
                                                              : (s->position <= upper_bound(target, tolerance));

        if (reached) {
            complete_move(e, i, HAL_MOTOR_EVENT_ARRIVED, HAL_MOTOR_END_POSITION);
            return;
        }
    }
    if (s->spec.use_soft_limit && mc->has_soft_limit) {
        if (s->dir == HAL_MOTOR_DIR_FORWARD && s->position >= mc->soft_max) {
            complete_move(e, i, HAL_MOTOR_EVENT_ARRIVED, HAL_MOTOR_END_SOFT_LIMIT);
            return;
        }
        if (s->dir == HAL_MOTOR_DIR_REVERSE && s->position <= mc->soft_min) {
            complete_move(e, i, HAL_MOTOR_EVENT_ARRIVED, HAL_MOTOR_END_SOFT_LIMIT);
            return;
        }
    }
    if (s->spec.use_current) {
        uint64_t run_ms = s->elapsed_ms + (e->now - s->move_start_ms);
        if (run_ms < (uint64_t)s->spec.current_blank_ms) {
            s->cur_stop_ms = 0;
        } else {
            int cur = drv_current(motor_drv(e, i));
            if (cur > s->spec.current_limit) {
                s->cur_stop_ms += (uint32_t)e->cfg.tick_ms;
                if (s->cur_stop_ms >= s->spec.current_confirm_ms) {
                    complete_move(e, i, HAL_MOTOR_EVENT_ARRIVED, HAL_MOTOR_END_CURRENT);
                    return;
                }
            } else {
                s->cur_stop_ms = 0;
            }
        }
    }
    uint64_t el = s->elapsed_ms + (e->now - s->move_start_ms);
    if (s->spec.use_time && el >= s->spec.duration_ms) {
        complete_move(e, i, HAL_MOTOR_EVENT_ARRIVED, HAL_MOTOR_END_TIME);
        return;
    }
    uint64_t max_t = s->spec.max_time_ms ? s->spec.max_time_ms : (uint64_t)mc->default_max_time_ms;
    if (el >= max_t) {
        complete_move(e, i, HAL_MOTOR_EVENT_TIMEOUT, HAL_MOTOR_END_TIMEOUT);
        return;
    }
}

/* ------------------------- 编码器 ------------------------- */

static void update_encoder(motor_executor_t *e, int i, bool moving)
{
    motor_encoder_t         *enc = motor_enc(e, i);
    const motor_motor_cfg_t *mc;
    motor_mstate_t          *s;
    int64_t                  r;
    int64_t                  d;
    int64_t                  ad;

    if (!enc) {
        return;
    }
    s  = &e->m[i];
    mc = &e->cfg.motors[i];
    r  = enc_raw(enc);

    if (mc->encoder_kind == MOTOR_ENC_ABSOLUTE) {
        /* 绝对行程：运动/静止均直接采纳传感器值 */
        (void)moving;
        s->position = r;
        s->last_raw = r;
        return;
    }

    d           = r - s->last_raw;
    s->last_raw = r;
    if (!moving) {
        return;
    }
    s->position += (s->dir == HAL_MOTOR_DIR_FORWARD) ? d : -d;
    ad = motor_iabs64(d);
    if (mc->enc_stall_ticks > 0) {
        if (ad == 0) {
            if (++s->enc_stall >= mc->enc_stall_ticks && !s->enc_warned) {
                s->enc_warned  = true;
                s->enc_healthy = false;
                warn(e, i, HAL_MOTOR_FAULT_ENCODER_SIGNAL);
            }
        } else {
            s->enc_stall = 0;
        }
    }
    if (mc->enc_jump_max > 0 && ad > mc->enc_jump_max && !s->enc_warned) {
        s->enc_warned  = true;
        s->enc_healthy = false;
        warn(e, i, HAL_MOTOR_FAULT_ENCODER_SIGNAL);
    }
}

/* ------------------------- 监测 ------------------------- */

static void monitor(motor_executor_t *e, int i)
{
    motor_mstate_t            *s  = &e->m[i];
    const motor_monitor_cfg_t *mn = &e->cfg.motors[i].mon;

    if ((e->now - s->start_ms) < (uint64_t)mn->startup_delay_ms) {
        /* 启动延迟窗口内不做电流判定。 */
    } else if (mn->monitor_current) {
        bool accel_seg  = (e->now - s->start_ms) < (uint64_t)e->cfg.motors[i].accel_ms;
        int  cmax       = accel_seg ? mn->cur_max_accel : mn->cur_max_steady;
        int  cmin       = accel_seg ? mn->cur_min_accel : mn->cur_min_steady;
        int  cur        = drv_current(motor_drv(e, i));
        s->cur_over_ms  = (cur > cmax) ? s->cur_over_ms + e->cfg.tick_ms : 0;
        s->cur_under_ms = (cur < cmin) ? s->cur_under_ms + e->cfg.tick_ms : 0;
        if (s->cur_over_ms >= mn->cur_confirm_ms && cur > cmax) {
            enter_fault(e, i, HAL_MOTOR_FAULT_OVERCURRENT);
            return;
        }
        if (s->cur_under_ms >= mn->cur_confirm_ms && cur < cmin) {
            enter_fault(e, i, HAL_MOTOR_FAULT_UNDERCURRENT);
            return;
        }
    }

    if (mn->monitor_feedback) {
        if (!drv_is_running(motor_drv(e, i))) {
            s->fb_bad_ms += e->cfg.tick_ms;
            if (s->fb_bad_ms >= mn->fb_confirm_ms) {
                s->fb_bad_ms = 0;
                if (++s->fb_strikes > mn->fb_retries) {
                    enter_fault(e, i, HAL_MOTOR_FAULT_DRIVER_FEEDBACK);
                    return;
                }
            }
        } else {
            s->fb_bad_ms = 0;
        }
    }

    if (mn->monitor_temp) {
        int t = 0;
        if (drv_temperature(motor_drv(e, i), &t)) {
            s->temp_bad_ms = (t > mn->temp_max) ? s->temp_bad_ms + e->cfg.tick_ms : 0;
            if (s->temp_bad_ms >= mn->temp_confirm_ms && t > mn->temp_max) {
                enter_fault(e, i, HAL_MOTOR_FAULT_OVERTEMP);
                return;
            }
        }
    }

    if (mn->monitor_voltage) {
        int v = 0;
        if (drv_voltage(motor_drv(e, i), &v)) {
            s->volt_bad_ms = (v < mn->volt_min) ? s->volt_bad_ms + e->cfg.tick_ms : 0;
            if (s->volt_bad_ms >= mn->volt_confirm_ms && v < mn->volt_min) {
                enter_fault(e, i, HAL_MOTOR_FAULT_UNDERVOLTAGE);
                return;
            }
        }
    }
}

/* ------------------------- 急停 / 看门狗 ------------------------- */

static void trigger_estop(motor_executor_t *e)
{
    for (int i = 0; i < e->motor_count; ++i) {
        motor_mstate_t *s          = &e->m[i];
        bool            was_active = (s->phase != HAL_MOTOR_PHASE_STOPPED && s->phase != HAL_MOTOR_PHASE_FAULT);
        (void)immediate_cut(e, i);
        /* 结算耗时后再转入急停态，急停事件才能带上被切断时的已运行时长。 */
        settle_elapsed(e, i);
        s->queued      = false;
        s->move_active = false;
        s->phase       = HAL_MOTOR_PHASE_ESTOP;
        if (was_active) {
            push_event(e, i, HAL_MOTOR_EVENT_ESTOP, HAL_MOTOR_END_NONE, HAL_MOTOR_FAULT_NONE);
        }
    }
    e->estop_latched = true;
}

static void trigger_safe(motor_executor_t *e)
{
    for (int i = 0; i < e->motor_count; ++i) {
        bool was_active = (e->m[i].phase != HAL_MOTOR_PHASE_STOPPED && e->m[i].phase != HAL_MOTOR_PHASE_FAULT
                           && e->m[i].phase != HAL_MOTOR_PHASE_ESTOP);
        (void)immediate_cut(e, i);
        e->m[i].queued      = false;
        e->m[i].move_active = false;
        fault_one(e, i, HAL_MOTOR_FAULT_WATCHDOG, false);
        if (was_active) {
            push_event(e, i, HAL_MOTOR_EVENT_FAULT, HAL_MOTOR_END_NONE, HAL_MOTOR_FAULT_WATCHDOG);
        }
    }
    e->safe_latched = true;
}

/* ------------------------- tick ------------------------- */

void motor_tick(motor_executor_t *e)
{
    e->now = clock_now(e);

    /* 看门狗：检测 tick 缺拍。 */
    if (!e->safe_latched && e->last_tick_valid && (e->now - e->last_tick_ms) > (uint64_t)e->cfg.watchdog_ms) {
        trigger_safe(e);
    }
    e->last_tick_ms    = e->now;
    e->last_tick_valid = true;

    if (e->safe_latched) {
        motor_dispatch(e);
        return;
    }

    /* 急停通道（最高优先级）。 */
    bool es = estop_active(e);
    if (es && !e->estop_latched) {
        trigger_estop(e);
    }
    if (e->estop_latched) {
        motor_dispatch(e);
        return;
    }

    for (int i = 0; i < e->motor_count; ++i) {
        motor_mstate_t *s = &e->m[i];

        /* 端口层致命错误。 */
        if (!s->fatal && drv_status(motor_drv(e, i)) == MOTOR_PORT_FATAL) {
            enter_fatal(e, i, HAL_MOTOR_FAULT_DRIVER_PORT_FATAL);
            continue;
        }
        if (s->phase == HAL_MOTOR_PHASE_FAULT || s->phase == HAL_MOTOR_PHASE_ESTOP) {
            continue;
        }

        switch (s->phase) {
        case HAL_MOTOR_PHASE_WAITING_START:
            if (e->now >= s->cooldown_until && interlock_ok(e, i) && s->queued) {
                begin_start(e, i, &s->pending);
            }
            break;
        case HAL_MOTOR_PHASE_REVERSAL_WAIT:
            if (e->now >= s->reversal_until) {
                begin_start(e, i, &s->after_reversal);
            }
            break;
        case HAL_MOTOR_PHASE_RUNNING:
            if (drv_poll(motor_drv(e, i), i) == MOTOR_PREPARE_FAILED) {
                enter_fault(e, i, HAL_MOTOR_FAULT_PREPARE_FAILED);
                break;
            }
            update_encoder(e, i, true);
            check_end(e, i);
            if (s->phase == HAL_MOTOR_PHASE_RUNNING) {
                if (!apply_output(e, i)) {
                    enter_fault(e, i, HAL_MOTOR_FAULT_DRIVER_PORT_FATAL);
                    break;
                }
                monitor(e, i);
            }
            break;
        case HAL_MOTOR_PHASE_DECELERATING:
            update_encoder(e, i, true);
            finish_halt(e, i);
            break;
        default:
            update_encoder(e, i, false);
            break;
        }
    }
    motor_dispatch(e);
}

/* ------------------------- 初始化 ------------------------- */

static motor_init_result_t init_ok(void)
{
    motor_init_result_t r;
    r.ok    = true;
    r.error = "";
    return r;
}

motor_init_result_t init_err(const char *msg)
{
    motor_init_result_t r;
    r.ok    = false;
    r.error = msg;
    return r;
}

/** @brief 执行 fail-fast 校验并把状态归零。cfg/ports 已存入 exec。 */
static motor_init_result_t do_init(motor_executor_t *e)
{
    const motor_config_t *c = &e->cfg;

    if (c->motor_count <= 0 || c->motor_count > MOTOR_MAX_MOTORS) {
        return init_err("no motors");
    }
    if (c->tick_ms <= 0) {
        return init_err("tickMs must be > 0");
    }
    if (c->watchdog_ms <= 0) {
        return init_err("watchdogMs must be > 0");
    }
    if (c->driver_count <= 0 || c->driver_count > MOTOR_MAX_DRIVERS) {
        return init_err("driverCount mismatch");
    }
    if (c->interlock_count < 0 || c->interlock_count > MOTOR_MAX_INTERLOCKS) {
        return init_err("interlock count out of range");
    }

    for (int i = 0; i < c->motor_count; ++i) {
        const motor_motor_cfg_t *mc = &c->motors[i];
        if (mc->driver_index < 0 || mc->driver_index >= c->driver_count) {
            return init_err("motor driverIndex out of range");
        }
        if (!e->ports.drivers[mc->driver_index]) {
            return init_err("driver port missing");
        }
        if (mc->default_max_time_ms <= 0) {
            return init_err("defaultMaxTimeMs must be > 0");
        }
        if (mc->cooldown_ms < 0 || mc->reversal_stop_ms < 0 || mc->accel_ms < 0 || mc->pos_tolerance < 0
            || mc->decel_point < 0) {
            return init_err("time params must be >= 0");
        }
        if ((mc->position_slow_gear < 0) || (mc->position_slow_gear > mc->gear_count)) {
            return init_err("position slow gear out of range");
        }
        if (mc->has_soft_limit && (mc->soft_min > mc->soft_max)) {
            return init_err("soft limit range invalid");
        }
        if (mc->has_encoder && !e->ports.encoders[i]) {
            return init_err("encoder port missing");
        }
        if (mc->mon.monitor_current && mc->mon.cur_max_steady <= mc->mon.cur_min_steady) {
            return init_err("current max must be > min");
        }
        if (mc->mon.monitor_current && mc->mon.cur_max_accel <= mc->mon.cur_min_accel) {
            return init_err("accel current max must be > min");
        }
    }

    for (int k = 0; k < c->interlock_count; ++k) {
        const motor_interlock_t *il = &c->interlocks[k];
        if (il->a < 0 || il->a >= c->motor_count || il->b < 0 || il->b >= c->motor_count) {
            return init_err("interlock references unknown motor");
        }
    }

    /* 归零（上电默认态：全部停止且输出关断）。 */
    e->motor_count = c->motor_count;
    for (int i = 0; i < c->motor_count; ++i) {
        motor_mstate_t empty = {0};
        e->m[i]              = empty;
        e->m[i].phase        = HAL_MOTOR_PHASE_STOPPED;
        e->m[i].dir          = HAL_MOTOR_DIR_FORWARD;
        e->m[i].fault_code   = HAL_MOTOR_FAULT_NONE;
    }
    e->now             = clock_now(e);
    e->last_tick_ms    = e->now;
    e->last_tick_valid = true;
    e->estop_latched   = false;
    e->safe_latched    = false;
    e->ev_head         = 0;
    e->ev_count        = 0;
    e->in_dispatch     = false;
    for (int i = 0; i < c->motor_count; ++i) {
        motor_encoder_t *enc = motor_enc(e, i);
        e->m[i].last_raw     = enc ? enc_raw(enc) : 0;
        /* 上电默认编码器健康：增量轴此时基准尚未建立，但这不是编码器异常。 */
        e->m[i].enc_healthy = true;
        if (!c->motors[i].has_encoder) {
            /* 无编码器机构无位置基准，与查询契约一致恒可信。 */
            e->m[i].baseline_trusted = true;
        } else if (enc && (c->motors[i].encoder_kind == MOTOR_ENC_ABSOLUTE)) {
            e->m[i].position         = e->m[i].last_raw;
            e->m[i].baseline_trusted = true;
        }
        if (drv_cutoff(motor_drv(e, i)) != SW_OK) {
            return init_err("driver cutoff failed");
        }
    }
    return init_ok();
}

motor_init_result_t motor_init(motor_executor_t *e, const motor_config_t *cfg, const motor_ports_t *ports)
{
    e->cfg    = *cfg;
    e->ports  = *ports;
    e->cb     = NULL;
    e->cb_ctx = NULL;
    return do_init(e);
}

motor_init_result_t motor_reinit(motor_executor_t *e)
{
    return do_init(e);
}
