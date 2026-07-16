/**
 * @file motor_executor.c
 * @brief 通用电机运动控制模块 —— 领域层实现。
 *
 * 采用显式状态机驱动，逐 tick 推进。除急停外命令均为异步语义。
 * 关键控制逻辑（启动/换向/停止/故障/急停/看门狗）均以状态迁移表达，
 * 上电默认态与故障安全态均为“停止且输出关断”。
 */
#include "adapters/outbound/hal/providers/mcc/motor_executor.h"
#include <limits.h>

/* 加减速步进哨兵：表示“无斜坡，立即到目标”。 */
#define MOTOR_RAMP_INSTANT  INT_MAX

/* ------------------------- 小工具 ------------------------- */

/** @brief 64 位绝对值。 */
static int64_t motor_iabs64(int64_t v) {
    return v < 0 ? -v : v;
}

/** @brief 取二者较小值。 */
static int motor_imin(int a, int b) {
    return a < b ? a : b;
}

/* ------------------------- 端口封装（可选方法 NULL 安全） ------------------------- */

static motor_driver_t *motor_drv(motor_executor_t *e, int i) {
    return e->ports.drivers[e->cfg.motors[i].driver_index];
}

static void drv_set_output(motor_driver_t *d, int freq, motor_direction_t dir) {
    d->set_output(d->ctx, freq, dir);
}

static void drv_cutoff(motor_driver_t *d) {
    d->cutoff(d->ctx);
}

static bool drv_reset(motor_driver_t *d) {
    return d->reset(d->ctx);
}

static bool drv_prepare(motor_driver_t *d) {
    return d->prepare ? d->prepare(d->ctx) : true;
}

static bool drv_is_running(motor_driver_t *d) {
    return d->is_running(d->ctx);
}

static int drv_current(motor_driver_t *d) {
    return d->current(d->ctx);
}

static bool drv_temperature(motor_driver_t *d, int *out) {
    return d->temperature ? d->temperature(d->ctx, out) : false;
}

static bool drv_voltage(motor_driver_t *d, int *out) {
    return d->voltage ? d->voltage(d->ctx, out) : false;
}

static motor_port_status_t drv_status(motor_driver_t *d) {
    return d->status ? d->status(d->ctx) : MOTOR_PORT_OK;
}

static int64_t enc_raw(motor_encoder_t *e) {
    return e->raw(e->ctx);
}

static bool enc_zero(motor_encoder_t *e) {
    return e->zero(e->ctx);
}

static motor_encoder_t *motor_enc(motor_executor_t *e, int i) {
    return e->ports.encoders[i];
}

static bool sensor_limit(motor_executor_t *e, int i, motor_limit_kind_t k) {
    return e->ports.sensors->limit(e->ports.sensors->ctx, i, k);
}

static bool estop_active(motor_executor_t *e) {
    return e->ports.estop->active(e->ports.estop->ctx);
}

static uint64_t clock_now(motor_executor_t *e) {
    return e->ports.clock->now_ms(e->ports.clock->ctx);
}

/* ------------------------- 结果构造 ------------------------- */

static motor_cmd_result_t cmd_make(motor_cmd_status_t st, const char *reason) {
    motor_cmd_result_t r;
    r.status = st;
    r.reason = reason;
    return r;
}

static motor_cmd_result_t cmd_reject(const char *reason) {
    return cmd_make(MOTOR_CMD_REJECTED, reason);
}

/* ------------------------- 事件队列（环形缓冲） ------------------------- */

static void ev_push(motor_executor_t *e, const motor_event_t *ev) {
    if (e->ev_count >= MOTOR_EVENT_QUEUE_CAP) {
        /* 队列满：丢弃最旧事件以容纳新事件（保证最新状态可达）。 */
        e->ev_head = (e->ev_head + 1) % MOTOR_EVENT_QUEUE_CAP;
        e->ev_count--;
    }
    int tail = (e->ev_head + e->ev_count) % MOTOR_EVENT_QUEUE_CAP;
    e->events[tail] = *ev;
    e->ev_count++;
}

static uint64_t eff_elapsed(motor_executor_t *e, int i) {
    motor_mstate_t *s = &e->m[i];
    if (s->phase == MOTOR_PHASE_RUNNING && s->moveActive) {
        return s->paused_elapsed_ms + (e->now - s->move_start_ms);
    }
    return s->paused_elapsed_ms;
}

static void push_event(motor_executor_t *e, int i, motor_event_type_t t,
                       motor_end_condition_t trig, motor_fault_code_t fc,
                       motor_fault_level_t lvl) {
    motor_event_t ev;
    ev.motor = i;
    ev.type = t;
    ev.trigger = trig;
    ev.final_pos = e->m[i].position;
    ev.elapsed_ms = eff_elapsed(e, i);
    ev.fault = fc;
    ev.level = lvl;
    ev_push(e, &ev);
}

/* ------------------------- 速度解析 ------------------------- */

static int resolve_speed(motor_executor_t *e, int i, motor_speed_t spd) {
    if (spd.kind == MOTOR_SPEED_FREQ) {
        return spd.value;
    }
    const motor_motor_cfg_t *mc = &e->cfg.motors[i];
    if (spd.value < 0 || spd.value >= mc->gear_count) {
        return -1;
    }
    return mc->gear_freq[spd.value];
}

/* ------------------------- 输出 / 加减速 ------------------------- */

static void apply_output(motor_executor_t *e, int i) {
    motor_mstate_t *s = &e->m[i];
    if (s->cur_freq > 0) {
        drv_set_output(motor_drv(e, i), s->cur_freq, s->dir);
    } else {
        drv_cutoff(motor_drv(e, i));
    }
}

static void immediate_cut(motor_executor_t *e, int i) {
    e->m[i].cur_freq = 0;
    drv_cutoff(motor_drv(e, i));
}

static int ramp_step(motor_executor_t *e, int ref_freq, int ramp_ms) {
    if (ramp_ms <= 0) {
        return MOTOR_RAMP_INSTANT;
    }
    int64_t step = (int64_t)ref_freq * e->cfg.tick_ms / ramp_ms;
    return step < 1 ? 1 : (int)step;
}

static int effective_target(motor_executor_t *e, int i) {
    motor_mstate_t *s = &e->m[i];
    const motor_motor_cfg_t *mc = &e->cfg.motors[i];
    int t = s->target_freq;
    if (s->moveActive && s->spec.use_position && mc->decel_point > 0 && mc->slow_freq > 0) {
        if (motor_iabs64(s->spec.target_pos - s->position) <= mc->decel_point) {
            t = motor_imin(t, mc->slow_freq);
        }
    }
    return t;
}

static void ramp_toward(motor_executor_t *e, int i, int target) {
    motor_mstate_t *s = &e->m[i];
    const motor_motor_cfg_t *mc = &e->cfg.motors[i];
    if (target > s->cur_freq) {
        int step = ramp_step(e, s->target_freq, mc->accel_ms);
        if (step == MOTOR_RAMP_INSTANT || s->cur_freq + step >= target) {
            s->cur_freq = target;
        } else {
            s->cur_freq += step;
        }
    } else if (target < s->cur_freq) {
        int step = ramp_step(e, s->target_freq, mc->decel_ms);
        if (step == MOTOR_RAMP_INSTANT || s->cur_freq - step <= target) {
            s->cur_freq = target;
        } else {
            s->cur_freq -= step;
        }
    }
}

/* ------------------------- 前向声明 ------------------------- */
static void fault_one(motor_executor_t *e, int i, motor_fault_code_t code, bool fatal);
static void link_shared_driver(motor_executor_t *e, int i);
static void enter_fault(motor_executor_t *e, int i, motor_fault_code_t code);
static void begin_start(motor_executor_t *e, int i, const motor_pending_cmd_t *pc);

/* ------------------------- 互锁 ------------------------- */

static bool interlock_ok(motor_executor_t *e, int i) {
    for (int k = 0; k < e->cfg.interlock_count; ++k) {
        const motor_interlock_t *il = &e->cfg.interlocks[k];
        if (il->a != i) {
            continue;
        }
        if (il->kind == MOTOR_INTERLOCK_MUTEX) {
            motor_phase_t bp = e->m[il->b].phase;
            if (bp == MOTOR_PHASE_RUNNING || bp == MOTOR_PHASE_DECELERATING ||
                bp == MOTOR_PHASE_WAITING_START || bp == MOTOR_PHASE_REVERSAL_WAIT ||
                bp == MOTOR_PHASE_PAUSED) {
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

static void begin_start(motor_executor_t *e, int i, const motor_pending_cmd_t *pc) {
    motor_mstate_t *s = &e->m[i];
    const motor_motor_cfg_t *mc = &e->cfg.motors[i];
    if (mc->prep_required && !drv_prepare(motor_drv(e, i))) {
        fault_one(e, i, MOTOR_FAULT_PREPARE_FAILED, false);
        push_event(e, i, MOTOR_EVENT_FAULT, MOTOR_END_NONE,
                   MOTOR_FAULT_PREPARE_FAILED, MOTOR_LEVEL_FAULT);
        link_shared_driver(e, i);
        return;
    }
    s->dir = pc->dir;
    s->target_freq = pc->freq;
    s->moveActive = pc->is_move;
    s->spec = pc->spec;
    s->move_start_ms = e->now;
    s->paused_elapsed_ms = 0;
    s->start_ms = e->now;
    s->emit_stop_on_halt = false;
    s->cur_over_ms = 0;
    s->cur_under_ms = 0;
    s->fb_bad_ms = 0;
    s->temp_bad_ms = 0;
    s->volt_bad_ms = 0;
    s->fb_strikes = 0;
    s->enc_stall = 0;
    s->enc_warned = false;
    s->queued = false;
    s->phase = MOTOR_PHASE_RUNNING;
}

static void reversal(motor_executor_t *e, int i, const motor_pending_cmd_t *pc) {
    motor_mstate_t *s = &e->m[i];
    immediate_cut(e, i);
    s->after_reversal = *pc;
    s->reversal_until = e->now + e->cfg.motors[i].reversal_stop_ms;
    s->phase = MOTOR_PHASE_REVERSAL_WAIT;
}

static bool in_cooldown(motor_executor_t *e, int i) {
    return e->now < e->m[i].cooldown_until;
}

static motor_cmd_result_t start_or_reverse(motor_executor_t *e, int i,
                                           const motor_pending_cmd_t *pc) {
    motor_mstate_t *s = &e->m[i];
    if (s->phase == MOTOR_PHASE_RUNNING || s->phase == MOTOR_PHASE_DECELERATING) {
        if (pc->dir != s->dir) {
            reversal(e, i, pc);
            return cmd_make(MOTOR_CMD_ACCEPTED, "reversal");
        }
        begin_start(e, i, pc);
        return cmd_make(MOTOR_CMD_ACCEPTED, "restart");
    }
    if (s->phase == MOTOR_PHASE_STOPPED) {
        if (!interlock_ok(e, i)) {
            return cmd_reject("interlock");
        }
        if (in_cooldown(e, i)) {
            s->pending = *pc;
            s->queued = true;
            s->phase = MOTOR_PHASE_WAITING_START;
            return cmd_make(MOTOR_CMD_QUEUED, "cooldown");
        }
        begin_start(e, i, pc);
        return cmd_make(MOTOR_CMD_ACCEPTED, "start");
    }
    return cmd_reject("bad-phase");
}

/* ------------------------- 故障 ------------------------- */

static void fault_one(motor_executor_t *e, int i, motor_fault_code_t code, bool fatal) {
    motor_mstate_t *s = &e->m[i];
    immediate_cut(e, i);
    s->phase = MOTOR_PHASE_FAULT;
    s->fault = true;
    s->fatal = fatal;
    s->fault_code = code;
    s->moveActive = false;
    s->queued = false;
    s->driver_reset_done = false;
}

static void link_shared_driver(motor_executor_t *e, int i) {
    int di = e->cfg.motors[i].driver_index;
    for (int j = 0; j < e->motor_count; ++j) {
        if (j == i) {
            continue;
        }
        if (e->cfg.motors[j].driver_index != di) {
            continue;
        }
        if (e->m[j].phase == MOTOR_PHASE_FAULT || e->m[j].phase == MOTOR_PHASE_ESTOP) {
            continue;
        }
        fault_one(e, j, MOTOR_FAULT_SHARED_DRIVER, false);
        push_event(e, j, MOTOR_EVENT_FAULT, MOTOR_END_NONE,
                   MOTOR_FAULT_SHARED_DRIVER, MOTOR_LEVEL_FAULT);
    }
}

static void enter_fault(motor_executor_t *e, int i, motor_fault_code_t code) {
    fault_one(e, i, code, false);
    push_event(e, i, MOTOR_EVENT_FAULT, MOTOR_END_NONE, code, MOTOR_LEVEL_FAULT);
    link_shared_driver(e, i);
}

static void enter_fatal(motor_executor_t *e, int i, motor_fault_code_t code) {
    fault_one(e, i, code, true);
    push_event(e, i, MOTOR_EVENT_FAULT, MOTOR_END_NONE, code, MOTOR_LEVEL_FATAL);
}

static void warn(motor_executor_t *e, int i, motor_fault_code_t code) {
    if (e->cfg.motors[i].enc_escalate && code == MOTOR_FAULT_ENCODER_SIGNAL) {
        enter_fault(e, i, code);
        return;
    }
    push_event(e, i, MOTOR_EVENT_WARNING, MOTOR_END_NONE, code, MOTOR_LEVEL_WARNING);
}

/* ------------------------- 停止 / 到位 ------------------------- */

static void finish_halt(motor_executor_t *e, int i) {
    motor_mstate_t *s = &e->m[i];
    immediate_cut(e, i);
    s->phase = MOTOR_PHASE_STOPPED;
    s->moveActive = false;
    s->cooldown_until = e->now + e->cfg.motors[i].cooldown_ms;
    if (s->emit_stop_on_halt) {
        s->emit_stop_on_halt = false;
        push_event(e, i, MOTOR_EVENT_STOPPED, MOTOR_END_NONE,
                   MOTOR_FAULT_NONE, MOTOR_LEVEL_WARNING);
    }
}

static void complete_move(motor_executor_t *e, int i, motor_event_type_t type,
                          motor_end_condition_t trig) {
    motor_mstate_t *s = &e->m[i];
    /* 事件先于停机动作上报，保证载荷（最终位置/耗时/触发条件）反映到位瞬间。 */
    motor_fault_level_t lvl = (type == MOTOR_EVENT_ARRIVED) ? MOTOR_LEVEL_WARNING
                                                             : MOTOR_LEVEL_FAULT;
    push_event(e, i, type, trig, MOTOR_FAULT_NONE, lvl);
    bool was_homing = s->homing;
    s->homing = false;
    s->moveActive = false;
    s->emit_stop_on_halt = false;
    if (was_homing && trig == MOTOR_END_LIMIT) {
        /* 回原点：清零并建立可信基准。 */
        motor_encoder_t *enc = motor_enc(e, i);
        if (enc) {
            (void)enc_zero(enc);
        }
        s->position = 0;
        s->last_raw = enc ? enc_raw(enc) : 0;
        s->baseline_trusted = true;
    }
    if (trig == MOTOR_END_LIMIT || trig == MOTOR_END_SOFT_LIMIT) {
        finish_halt(e, i); /* 限位/软限位：触发瞬间停止 */
    } else {
        s->phase = MOTOR_PHASE_DECELERATING; /* 位置/时间/超时：减速停机 */
    }
}

static void check_end(motor_executor_t *e, int i) {
    motor_mstate_t *s = &e->m[i];
    const motor_motor_cfg_t *mc = &e->cfg.motors[i];
    if (!s->moveActive) {
        return;
    }
    /* 优先级：限位 → 位置 → 软限位 → 时间 → 超时兜底 */
    if (s->spec.use_limit && sensor_limit(e, i, s->spec.limit)) {
        complete_move(e, i, MOTOR_EVENT_ARRIVED, MOTOR_END_LIMIT);
        return;
    }
    if (s->spec.use_position &&
        motor_iabs64(s->position - s->spec.target_pos) <= mc->pos_tolerance) {
        complete_move(e, i, MOTOR_EVENT_ARRIVED, MOTOR_END_POSITION);
        return;
    }
    if (s->spec.use_soft_limit && mc->has_soft_limit) {
        if (s->dir == MOTOR_DIR_FORWARD && s->position >= mc->soft_max) {
            complete_move(e, i, MOTOR_EVENT_ARRIVED, MOTOR_END_SOFT_LIMIT);
            return;
        }
        if (s->dir == MOTOR_DIR_REVERSE && s->position <= mc->soft_min) {
            complete_move(e, i, MOTOR_EVENT_ARRIVED, MOTOR_END_SOFT_LIMIT);
            return;
        }
    }
    uint64_t el = s->paused_elapsed_ms + (e->now - s->move_start_ms);
    if (s->spec.use_time && el >= s->spec.duration_ms) {
        complete_move(e, i, MOTOR_EVENT_ARRIVED, MOTOR_END_TIME);
        return;
    }
    uint64_t max_t = s->spec.max_time_ms ? s->spec.max_time_ms
                                         : (uint64_t)mc->default_max_move_ms;
    if (el >= max_t) {
        complete_move(e, i, MOTOR_EVENT_TIMEOUT, MOTOR_END_TIMEOUT);
        return;
    }
}

/* ------------------------- 编码器 ------------------------- */

static void update_encoder(motor_executor_t *e, int i, bool moving) {
    motor_encoder_t *enc = motor_enc(e, i);
    if (!enc) {
        return;
    }
    motor_mstate_t *s = &e->m[i];
    int64_t r = enc_raw(enc);
    int64_t d = r - s->last_raw;
    s->last_raw = r;
    if (!moving) {
        return;
    }
    s->position += (s->dir == MOTOR_DIR_FORWARD) ? d : -d;
    const motor_motor_cfg_t *mc = &e->cfg.motors[i];
    int64_t ad = motor_iabs64(d);
    if (mc->enc_stall_ticks > 0) {
        if (ad == 0) {
            if (++s->enc_stall >= mc->enc_stall_ticks && !s->enc_warned) {
                s->enc_warned = true;
                warn(e, i, MOTOR_FAULT_ENCODER_SIGNAL);
            }
        } else {
            s->enc_stall = 0;
        }
    }
    if (mc->enc_jump_max > 0 && ad > mc->enc_jump_max && !s->enc_warned) {
        s->enc_warned = true;
        warn(e, i, MOTOR_FAULT_ENCODER_SIGNAL);
    }
}

/* ------------------------- 监测 ------------------------- */

static void monitor(motor_executor_t *e, int i) {
    motor_mstate_t *s = &e->m[i];
    const motor_monitor_cfg_t *mn = &e->cfg.motors[i].mon;

    if ((e->now - s->start_ms) < (uint64_t)mn->startup_delay_ms) {
        /* 启动延迟窗口内不做电流判定。 */
    } else if (mn->monitor_current) {
        bool accel_seg = s->cur_freq < s->target_freq;
        int cmax = accel_seg ? mn->cur_max_accel : mn->cur_max_steady;
        int cmin = accel_seg ? mn->cur_min_accel : mn->cur_min_steady;
        int cur = drv_current(motor_drv(e, i));
        s->cur_over_ms = (cur > cmax) ? s->cur_over_ms + e->cfg.tick_ms : 0;
        s->cur_under_ms = (cur < cmin) ? s->cur_under_ms + e->cfg.tick_ms : 0;
        if (s->cur_over_ms >= mn->cur_confirm_ms && cur > cmax) {
            enter_fault(e, i, MOTOR_FAULT_OVERCURRENT);
            return;
        }
        if (s->cur_under_ms >= mn->cur_confirm_ms && cur < cmin) {
            enter_fault(e, i, MOTOR_FAULT_UNDERCURRENT);
            return;
        }
    }

    if (mn->monitor_feedback) {
        if (!drv_is_running(motor_drv(e, i))) {
            s->fb_bad_ms += e->cfg.tick_ms;
            if (s->fb_bad_ms >= mn->fb_confirm_ms) {
                s->fb_bad_ms = 0;
                if (++s->fb_strikes > mn->fb_retries) {
                    enter_fault(e, i, MOTOR_FAULT_DRIVER_FEEDBACK);
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
                enter_fault(e, i, MOTOR_FAULT_OVERTEMP);
                return;
            }
        }
    }

    if (mn->monitor_voltage) {
        int v = 0;
        if (drv_voltage(motor_drv(e, i), &v)) {
            s->volt_bad_ms = (v < mn->volt_min) ? s->volt_bad_ms + e->cfg.tick_ms : 0;
            if (s->volt_bad_ms >= mn->volt_confirm_ms && v < mn->volt_min) {
                enter_fault(e, i, MOTOR_FAULT_UNDERVOLTAGE);
                return;
            }
        }
    }
}

/* ------------------------- 急停 / 看门狗 ------------------------- */

static void trigger_estop(motor_executor_t *e) {
    for (int i = 0; i < e->motor_count; ++i) {
        motor_mstate_t *s = &e->m[i];
        bool was_active = (s->phase != MOTOR_PHASE_STOPPED && s->phase != MOTOR_PHASE_FAULT);
        immediate_cut(e, i);
        s->queued = false;
        s->moveActive = false;
        s->phase = MOTOR_PHASE_ESTOP;
        if (was_active) {
            push_event(e, i, MOTOR_EVENT_ESTOP, MOTOR_END_NONE,
                       MOTOR_FAULT_NONE, MOTOR_LEVEL_FAULT);
        }
    }
    e->estop_latched = true;
}

static void trigger_safe(motor_executor_t *e) {
    for (int i = 0; i < e->motor_count; ++i) {
        bool was_active = (e->m[i].phase != MOTOR_PHASE_STOPPED &&
                           e->m[i].phase != MOTOR_PHASE_FAULT &&
                           e->m[i].phase != MOTOR_PHASE_ESTOP);
        immediate_cut(e, i);
        e->m[i].queued = false;
        e->m[i].moveActive = false;
        fault_one(e, i, MOTOR_FAULT_WATCHDOG, false);
        if (was_active) {
            push_event(e, i, MOTOR_EVENT_FAULT, MOTOR_END_NONE,
                       MOTOR_FAULT_WATCHDOG, MOTOR_LEVEL_FAULT);
        }
    }
    e->safe_latched = true;
}

/* ------------------------- 事件分发 ------------------------- */

static void dispatch(motor_executor_t *e) {
    if (!e->cb) {
        return;
    }
    e->in_dispatch = true;
    motor_event_t ev;
    while (e->ev_count > 0) {
        ev = e->events[e->ev_head];
        e->ev_head = (e->ev_head + 1) % MOTOR_EVENT_QUEUE_CAP;
        e->ev_count--;
        e->cb(&ev, e->cb_ctx);
    }
    e->in_dispatch = false;
}

/* ------------------------- tick ------------------------- */

void motor_tick(motor_executor_t *e) {
    e->now = clock_now(e);

    /* 看门狗：检测 tick 缺拍。 */
    if (!e->safe_latched && e->last_tick_valid &&
        (e->now - e->last_tick_ms) > (uint64_t)e->cfg.watchdog_ms) {
        trigger_safe(e);
    }
    e->last_tick_ms = e->now;
    e->last_tick_valid = true;

    if (e->safe_latched) {
        dispatch(e);
        return;
    }

    /* 急停通道（最高优先级）。 */
    bool es = estop_active(e);
    if (es && !e->estop_latched) {
        trigger_estop(e);
    }
    if (e->estop_latched) {
        dispatch(e);
        return;
    }

    for (int i = 0; i < e->motor_count; ++i) {
        motor_mstate_t *s = &e->m[i];

        /* 端口层致命错误。 */
        if (!s->fatal && drv_status(motor_drv(e, i)) == MOTOR_PORT_FATAL) {
            enter_fatal(e, i, MOTOR_FAULT_DRIVER_PORT_FATAL);
            continue;
        }
        if (s->phase == MOTOR_PHASE_FAULT || s->phase == MOTOR_PHASE_ESTOP) {
            continue;
        }

        switch (s->phase) {
            case MOTOR_PHASE_WAITING_START:
                if (e->now >= s->cooldown_until && interlock_ok(e, i) && s->queued) {
                    begin_start(e, i, &s->pending);
                }
                break;
            case MOTOR_PHASE_REVERSAL_WAIT:
                if (e->now >= s->reversal_until) {
                    begin_start(e, i, &s->after_reversal);
                }
                break;
            case MOTOR_PHASE_RUNNING:
                update_encoder(e, i, true);
                ramp_toward(e, i, effective_target(e, i));
                apply_output(e, i);
                check_end(e, i);
                if (s->phase == MOTOR_PHASE_RUNNING) {
                    monitor(e, i);
                }
                break;
            case MOTOR_PHASE_DECELERATING:
                update_encoder(e, i, true);
                ramp_toward(e, i, 0);
                apply_output(e, i);
                if (s->cur_freq == 0) {
                    finish_halt(e, i);
                }
                break;
            case MOTOR_PHASE_PAUSED:
                update_encoder(e, i, false);
                break;
            default:
                update_encoder(e, i, false);
                break;
        }
    }
    dispatch(e);
}

/* ------------------------- 初始化 ------------------------- */

static motor_init_result_t init_ok(void) {
    motor_init_result_t r;
    r.ok = true;
    r.error = "";
    return r;
}

static motor_init_result_t init_err(const char *msg) {
    motor_init_result_t r;
    r.ok = false;
    r.error = msg;
    return r;
}

static int monitor_channel_count(const motor_monitor_cfg_t *mn) {
    return (mn->monitor_current ? 1 : 0) + (mn->monitor_feedback ? 1 : 0)
         + (mn->monitor_temp ? 1 : 0) + (mn->monitor_voltage ? 1 : 0);
}

/** @brief 执行 fail-fast 校验并把状态归零。cfg/ports 已存入 exec。 */
static motor_init_result_t do_init(motor_executor_t *e) {
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

    int channels = 0;
    for (int i = 0; i < c->motor_count; ++i) {
        const motor_motor_cfg_t *mc = &c->motors[i];
        if (mc->driver_index < 0 || mc->driver_index >= c->driver_count) {
            return init_err("motor driverIndex out of range");
        }
        if (!e->ports.drivers[mc->driver_index]) {
            return init_err("driver port missing");
        }
        if (mc->default_max_move_ms <= 0) {
            return init_err("defaultMaxMoveMs must be > 0");
        }
        if (mc->cooldown_ms < 0 || mc->reversal_stop_ms < 0 ||
            mc->accel_ms < 0 || mc->decel_ms < 0) {
            return init_err("time params must be >= 0");
        }
        if (mc->cap_position_move && !mc->has_encoder) {
            return init_err("position-move capability requires encoder");
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
        channels += monitor_channel_count(&mc->mon);
    }
    if (c->max_monitor_channels > 0 && channels > c->max_monitor_channels) {
        return init_err("monitor channels exceed capacity");
    }

    for (int k = 0; k < c->interlock_count; ++k) {
        const motor_interlock_t *il = &c->interlocks[k];
        if (il->a < 0 || il->a >= c->motor_count ||
            il->b < 0 || il->b >= c->motor_count) {
            return init_err("interlock references unknown motor");
        }
    }

    /* 归零（上电默认态：全部停止且输出关断）。 */
    e->motor_count = c->motor_count;
    for (int i = 0; i < c->motor_count; ++i) {
        motor_mstate_t empty = {0};
        e->m[i] = empty;
        e->m[i].phase = MOTOR_PHASE_STOPPED;
        e->m[i].dir = MOTOR_DIR_FORWARD;
        e->m[i].fault_code = MOTOR_FAULT_NONE;
    }
    e->now = clock_now(e);
    e->last_tick_ms = e->now;
    e->last_tick_valid = true;
    e->estop_latched = false;
    e->safe_latched = false;
    e->ev_head = 0;
    e->ev_count = 0;
    e->in_dispatch = false;
    for (int i = 0; i < c->motor_count; ++i) {
        motor_encoder_t *enc = motor_enc(e, i);
        e->m[i].last_raw = enc ? enc_raw(enc) : 0;
        drv_cutoff(motor_drv(e, i));
    }
    e->initialized = true;
    return init_ok();
}

motor_init_result_t motor_init(motor_executor_t *e, const motor_config_t *cfg,
                               const motor_ports_t *ports) {
    e->cfg = *cfg;
    e->ports = *ports;
    e->cb = NULL;
    e->cb_ctx = NULL;
    return do_init(e);
}

motor_init_result_t motor_reinit(motor_executor_t *e) {
    return do_init(e);
}

/* ------------------------- 命令 ------------------------- */

static bool bad_motor(motor_executor_t *e, int i) {
    return i < 0 || i >= e->motor_count;
}

motor_cmd_result_t motor_run_continuous(motor_executor_t *e, int i,
                                        motor_speed_t spd, motor_direction_t dir) {
    if (e->in_dispatch) {
        return cmd_reject("reentrant");
    }
    if (bad_motor(e, i)) {
        return cmd_reject("bad-motor");
    }
    e->now = clock_now(e);
    if (e->estop_latched || e->safe_latched) {
        return cmd_reject("safety-locked");
    }
    if (e->m[i].phase == MOTOR_PHASE_FAULT) {
        return cmd_reject("fault");
    }
    if (e->m[i].phase == MOTOR_PHASE_PAUSED) {
        return cmd_reject("paused");
    }
    int f = resolve_speed(e, i, spd);
    if (f <= 0) {
        return cmd_reject("bad-speed");
    }
    motor_pending_cmd_t pc = {0};
    pc.is_move = false;
    pc.freq = f;
    pc.dir = dir;
    return start_or_reverse(e, i, &pc);
}

motor_cmd_result_t motor_move_to(motor_executor_t *e, int i, motor_speed_t spd,
                                 motor_direction_t dir, const motor_move_spec_t *spec) {
    if (e->in_dispatch) {
        return cmd_reject("reentrant");
    }
    if (bad_motor(e, i)) {
        return cmd_reject("bad-motor");
    }
    e->now = clock_now(e);
    if (e->estop_latched || e->safe_latched) {
        return cmd_reject("safety-locked");
    }
    if (e->m[i].phase == MOTOR_PHASE_FAULT) {
        return cmd_reject("fault");
    }
    if (e->m[i].phase == MOTOR_PHASE_PAUSED) {
        return cmd_reject("paused");
    }
    if (spec->use_position) {
        if (!e->cfg.motors[i].has_encoder) {
            return cmd_reject("no-encoder");
        }
        if (!e->m[i].baseline_trusted) {
            return cmd_reject("baseline-untrusted");
        }
    }
    int f = resolve_speed(e, i, spd);
    if (f <= 0) {
        return cmd_reject("bad-speed");
    }
    motor_pending_cmd_t pc = {0};
    pc.is_move = true;
    pc.freq = f;
    pc.dir = dir;
    pc.spec = *spec;
    return start_or_reverse(e, i, &pc);
}

motor_cmd_result_t motor_stop(motor_executor_t *e, int i) {
    if (e->in_dispatch) {
        return cmd_reject("reentrant");
    }
    if (bad_motor(e, i)) {
        return cmd_reject("bad-motor");
    }
    e->now = clock_now(e);
    motor_mstate_t *s = &e->m[i];
    switch (s->phase) {
        case MOTOR_PHASE_WAITING_START:
            s->queued = false;
            s->phase = MOTOR_PHASE_STOPPED;
            return cmd_make(MOTOR_CMD_ACCEPTED, "queue-cancel");
        case MOTOR_PHASE_REVERSAL_WAIT:
            s->phase = MOTOR_PHASE_STOPPED;
            s->cooldown_until = e->now + e->cfg.motors[i].cooldown_ms;
            return cmd_make(MOTOR_CMD_ACCEPTED, "reversal-cancel");
        case MOTOR_PHASE_RUNNING:
        case MOTOR_PHASE_DECELERATING:
            s->emit_stop_on_halt = true;
            s->moveActive = false;
            s->phase = MOTOR_PHASE_DECELERATING;
            return cmd_make(MOTOR_CMD_ACCEPTED, "stopping");
        case MOTOR_PHASE_PAUSED:
            s->emit_stop_on_halt = true;
            s->phase = MOTOR_PHASE_DECELERATING;
            s->cur_freq = 0;
            finish_halt(e, i);
            return cmd_make(MOTOR_CMD_ACCEPTED, "stopped");
        default:
            return cmd_make(MOTOR_CMD_ACCEPTED, "already-stopped");
    }
}

motor_cmd_result_t motor_pause(motor_executor_t *e, int i) {
    if (e->in_dispatch) {
        return cmd_reject("reentrant");
    }
    if (bad_motor(e, i)) {
        return cmd_reject("bad-motor");
    }
    e->now = clock_now(e);
    motor_mstate_t *s = &e->m[i];
    if (s->phase != MOTOR_PHASE_RUNNING) {
        return cmd_reject("not-running");
    }
    if (s->moveActive) {
        s->paused_elapsed_ms += e->now - s->move_start_ms;
    }
    immediate_cut(e, i);
    s->phase = MOTOR_PHASE_PAUSED;
    return cmd_make(MOTOR_CMD_ACCEPTED, "paused");
}

motor_cmd_result_t motor_resume(motor_executor_t *e, int i) {
    if (e->in_dispatch) {
        return cmd_reject("reentrant");
    }
    if (bad_motor(e, i)) {
        return cmd_reject("bad-motor");
    }
    e->now = clock_now(e);
    motor_mstate_t *s = &e->m[i];
    if (s->phase != MOTOR_PHASE_PAUSED) {
        return cmd_reject("not-paused");
    }
    s->move_start_ms = e->now;
    s->phase = MOTOR_PHASE_RUNNING;
    return cmd_make(MOTOR_CMD_ACCEPTED, "resumed");
}

motor_cmd_result_t motor_set_speed(motor_executor_t *e, int i,
                                   motor_speed_t spd, motor_direction_t dir) {
    if (e->in_dispatch) {
        return cmd_reject("reentrant");
    }
    if (bad_motor(e, i)) {
        return cmd_reject("bad-motor");
    }
    e->now = clock_now(e);
    motor_mstate_t *s = &e->m[i];
    if (s->phase != MOTOR_PHASE_RUNNING) {
        return cmd_reject("not-running");
    }
    int f = resolve_speed(e, i, spd);
    if (f <= 0) {
        return cmd_reject("bad-speed");
    }
    if (dir == s->dir) {
        s->target_freq = f;
        return cmd_make(MOTOR_CMD_ACCEPTED, "speed-changed");
    }
    motor_pending_cmd_t pc = {0};
    pc.is_move = s->moveActive;
    pc.spec = s->spec;
    pc.freq = f;
    pc.dir = dir;
    reversal(e, i, &pc);
    return cmd_make(MOTOR_CMD_ACCEPTED, "reversal");
}

motor_cmd_result_t motor_home(motor_executor_t *e, int i) {
    if (e->in_dispatch) {
        return cmd_reject("reentrant");
    }
    if (bad_motor(e, i)) {
        return cmd_reject("bad-motor");
    }
    e->now = clock_now(e);
    if (e->estop_latched || e->safe_latched) {
        return cmd_reject("safety-locked");
    }
    motor_mstate_t *s = &e->m[i];
    if (!e->cfg.motors[i].has_encoder) {
        return cmd_reject("no-encoder");
    }
    if (s->phase != MOTOR_PHASE_STOPPED) {
        return cmd_reject("not-stopped");
    }
    if (!interlock_ok(e, i)) {
        return cmd_reject("interlock");
    }
    motor_pending_cmd_t pc = {0};
    pc.is_move = true;
    pc.freq = e->cfg.motors[i].slow_freq > 0 ? e->cfg.motors[i].slow_freq
                                             : MOTOR_HOME_DEFAULT_FREQ_CENTI_HZ;
    pc.dir = MOTOR_DIR_REVERSE;
    pc.spec.use_limit = true;
    pc.spec.limit = MOTOR_LIMIT_ORIGIN;
    if (in_cooldown(e, i)) {
        s->pending = pc;
        s->queued = true;
        s->phase = MOTOR_PHASE_WAITING_START;
        s->homing = true;
        return cmd_make(MOTOR_CMD_QUEUED, "cooldown");
    }
    begin_start(e, i, &pc);
    s->homing = true;
    return cmd_make(MOTOR_CMD_ACCEPTED, "homing");
}

motor_cmd_result_t motor_zero_encoder(motor_executor_t *e, int i) {
    if (e->in_dispatch) {
        return cmd_reject("reentrant");
    }
    if (bad_motor(e, i)) {
        return cmd_reject("bad-motor");
    }
    motor_encoder_t *enc = motor_enc(e, i);
    if (!e->cfg.motors[i].has_encoder || !enc) {
        return cmd_reject("no-encoder");
    }
    if (e->m[i].moveActive && e->m[i].spec.use_position) {
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
    push_event(e, i, MOTOR_EVENT_WARNING, MOTOR_END_NONE,
               MOTOR_FAULT_ENCODER_SIGNAL, MOTOR_LEVEL_WARNING);
    return cmd_reject("zero-failed");
}

motor_cmd_result_t motor_confirm_baseline(motor_executor_t *e, int i) {
    if (bad_motor(e, i)) {
        return cmd_reject("bad-motor");
    }
    e->m[i].baseline_trusted = true;
    return cmd_make(MOTOR_CMD_ACCEPTED, "baseline-confirmed");
}

void motor_reset_estop(motor_executor_t *e) {
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

void motor_reset_watchdog(motor_executor_t *e) {
    if (e->in_dispatch) {
        return;
    }
    if (!e->safe_latched) {
        return;
    }
    e->safe_latched = false;
    e->now = clock_now(e);
    e->last_tick_ms = e->now;
    e->last_tick_valid = true;
    for (int i = 0; i < e->motor_count; ++i) {
        if (e->m[i].fault_code == MOTOR_FAULT_WATCHDOG) {
            e->m[i].phase = MOTOR_PHASE_STOPPED;
            e->m[i].fault = false;
            e->m[i].fault_code = MOTOR_FAULT_NONE;
        }
    }
}

motor_cmd_result_t motor_recover(motor_executor_t *e, int i, motor_recovery_step_t step) {
    if (e->in_dispatch) {
        return cmd_reject("reentrant");
    }
    if (bad_motor(e, i)) {
        return cmd_reject("bad-motor");
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
            push_event(e, i, MOTOR_EVENT_FAULT, MOTOR_END_NONE,
                       s->fault_code, MOTOR_LEVEL_FAULT);
            return cmd_reject("driver-reset-failed");
        }
        s->driver_reset_done = true;
        return cmd_make(MOTOR_CMD_ACCEPTED, "driver-reset");
    }
    /* MODULE_STOP */
    if (!s->driver_reset_done) {
        return cmd_reject("must-reset-first");
    }
    e->now = clock_now(e);
    s->phase = MOTOR_PHASE_STOPPED;
    s->fault = false;
    s->fault_code = MOTOR_FAULT_NONE;
    s->driver_reset_done = false;
    s->cooldown_until = e->now;
    return cmd_make(MOTOR_CMD_ACCEPTED, "recovered");
}

/* ------------------------- 查询 ------------------------- */

motor_phase_t motor_phase(const motor_executor_t *e, int i) {
    return e->m[i].phase;
}

int64_t motor_position(const motor_executor_t *e, int i) {
    return e->m[i].position;
}

int motor_current_freq(const motor_executor_t *e, int i) {
    return e->m[i].cur_freq;
}

motor_direction_t motor_direction(const motor_executor_t *e, int i) {
    return e->m[i].dir;
}

motor_fault_code_t motor_fault_code(const motor_executor_t *e, int i) {
    return e->m[i].fault_code;
}

bool motor_baseline_trusted(const motor_executor_t *e, int i) {
    return e->m[i].baseline_trusted;
}

bool motor_in_safe_state(const motor_executor_t *e) {
    return e->safe_latched;
}

/* ------------------------- 事件 ------------------------- */

void motor_set_event_callback(motor_executor_t *e, motor_event_cb_t cb, void *ctx) {
    if (e->in_dispatch) {
        return;
    }
    e->cb = cb;
    e->cb_ctx = ctx;
}

bool motor_pop_event(motor_executor_t *e, motor_event_t *out) {
    if (e->ev_count <= 0) {
        return false;
    }
    *out = e->events[e->ev_head];
    e->ev_head = (e->ev_head + 1) % MOTOR_EVENT_QUEUE_CAP;
    e->ev_count--;
    return true;
}
