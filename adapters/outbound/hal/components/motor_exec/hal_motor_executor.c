/**
 * @file hal_motor_executor.c
 * @brief 电机执行器组合件实现（状态机 + hal_motor_exec_port 符号）。
 *
 * 采用显式状态机驱动，逐 tick 推进。除急停外命令均为异步语义。
 * 关键控制逻辑（启动/换向/停止/故障/急停/看门狗）均以状态迁移表达，
 * 上电默认态与故障安全态均为“停止且输出关断”。
 * 本文件同时实现出站端口 hal_motor_*，供 domain patterns 直接调用。
 */
#include "adapters/outbound/hal/components/motor_exec/hal_motor_executor.h"
#include "domain/ports/outbound/motor/hal_motor_exec_port.h"

#include <limits.h>

/* ------------------------- 小工具 ------------------------- */

/** @brief 64 位绝对值。 */
static int64_t motor_iabs64(int64_t v)
{
    return v < 0 ? -v : v;
}

static bool within_distance(int64_t left, int64_t right, int distance)
{
    if (left >= right) {
        return (right > INT64_MAX - distance) || (left <= right + distance);
    }
    return (right < INT64_MIN + distance) || (left >= right - distance);
}

static int64_t lower_bound(int64_t value, int margin)
{
    return (value < INT64_MIN + margin) ? INT64_MIN : value - margin;
}

static int64_t upper_bound(int64_t value, int margin)
{
    return (value > INT64_MAX - margin) ? INT64_MAX : value + margin;
}

/* ------------------------- 端口封装（可选方法 NULL 安全） ------------------------- */

static motor_driver_t *motor_drv(motor_executor_t *e, int i)
{
    return e->ports.drivers[e->cfg.motors[i].driver_index];
}

static sw_err_t drv_set_output(motor_driver_t *d, motor_speed_t speed, motor_direction_t dir)
{
    return d->set_output(d->ctx, speed, dir);
}

static sw_err_t drv_cutoff(motor_driver_t *d)
{
    return d->cutoff(d->ctx);
}

static bool drv_reset(motor_driver_t *d)
{
    return d->reset(d->ctx);
}

static motor_prepare_result_t drv_prepare(motor_driver_t *d, int motor)
{
    return d->prepare ? d->prepare(d->ctx, motor) : MOTOR_PREPARE_READY;
}

static motor_prepare_result_t drv_poll(motor_driver_t *d, int motor)
{
    return d->poll ? d->poll(d->ctx, motor) : MOTOR_PREPARE_READY;
}

static bool drv_is_running(motor_driver_t *d)
{
    return d->is_running(d->ctx);
}

static int drv_current(motor_driver_t *d)
{
    return d->current(d->ctx);
}

static bool drv_temperature(motor_driver_t *d, int *out)
{
    return d->temperature ? d->temperature(d->ctx, out) : false;
}

static bool drv_voltage(motor_driver_t *d, int *out)
{
    return d->voltage ? d->voltage(d->ctx, out) : false;
}

static motor_port_status_t drv_status(motor_driver_t *d)
{
    return d->status ? d->status(d->ctx) : MOTOR_PORT_OK;
}

static int64_t enc_raw(motor_encoder_t *e)
{
    return e->raw(e->ctx);
}

static bool enc_zero(motor_encoder_t *e)
{
    return e->zero(e->ctx);
}

static motor_encoder_t *motor_enc(motor_executor_t *e, int i)
{
    return e->ports.encoders[i];
}

static bool sensor_limit(motor_executor_t *e, int i, motor_limit_kind_t k)
{
    return e->ports.sensors->limit(e->ports.sensors->ctx, i, k);
}

static bool estop_active(motor_executor_t *e)
{
    return e->ports.estop->active(e->ports.estop->ctx);
}

static uint64_t clock_now(motor_executor_t *e)
{
    return e->ports.clock->now_ms(e->ports.clock->ctx);
}

/* ------------------------- 结果构造 ------------------------- */

static motor_cmd_result_t cmd_make(motor_cmd_status_t st, const char *reason)
{
    motor_cmd_result_t r;
    r.status = st;
    r.reason = reason;
    return r;
}

static motor_cmd_result_t cmd_reject(const char *reason)
{
    return cmd_make(MOTOR_CMD_REJECTED, reason);
}

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
static void ev_push(motor_executor_t *e, const motor_event_t *ev)
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
static void settle_elapsed(motor_executor_t *e, int i)
{
    motor_mstate_t *s = &e->m[i];
    if (s->phase == MOTOR_PHASE_RUNNING) {
        s->elapsed_ms   += e->now - s->move_start_ms;
        s->move_start_ms = e->now;
    }
}

static void push_event(motor_executor_t     *e,
                       int                   i,
                       motor_event_type_t    t,
                       motor_end_condition_t trig,
                       motor_fault_code_t    fc,
                       motor_fault_level_t   lvl)
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
    ev.level      = lvl;
    ev_push(e, &ev);
}

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

/* ------------------------- 输出 ------------------------- */

static bool speed_equal(motor_speed_t left, motor_speed_t right)
{
    return (left.kind == right.kind) && (left.value == right.value);
}

static motor_speed_t desired_output_speed(motor_executor_t *e, int i)
{
    motor_mstate_t          *state = &e->m[i];
    const motor_motor_cfg_t *cfg   = &e->cfg.motors[i];
    motor_speed_t            speed = state->speed;

    if (state->move_active && state->spec.use_position && (speed.kind == MOTOR_SPEED_GEAR) && (cfg->decel_point > 0)
        && (cfg->position_slow_gear > 0) && within_distance(state->spec.target_pos, state->position, cfg->decel_point)
        && (speed.value > cfg->position_slow_gear)) {
        speed.value = cfg->position_slow_gear;
    }
    return speed;
}

static bool apply_output(motor_executor_t *e, int i)
{
    motor_mstate_t *s       = &e->m[i];
    motor_speed_t   desired = desired_output_speed(e, i);

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
static void fault_one(motor_executor_t *e, int i, motor_fault_code_t code, bool fatal);
static void link_shared_driver(motor_executor_t *e, int i);
static void enter_fault(motor_executor_t *e, int i, motor_fault_code_t code);
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
            motor_phase_t bp = e->m[il->b].phase;
            if (bp == MOTOR_PHASE_RUNNING || bp == MOTOR_PHASE_DECELERATING || bp == MOTOR_PHASE_WAITING_START
                || bp == MOTOR_PHASE_REVERSAL_WAIT) {
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
            s->phase   = MOTOR_PHASE_WAITING_START;
            return;
        }
        if (prep == MOTOR_PREPARE_FAILED) {
            fault_one(e, i, MOTOR_FAULT_PREPARE_FAILED, false);
            push_event(e, i, MOTOR_EVENT_FAULT, MOTOR_END_NONE, MOTOR_FAULT_PREPARE_FAILED, MOTOR_LEVEL_FAULT);
            link_shared_driver(e, i);
            return;
        }
    }

    s->dir               = pc->dir;
    s->speed             = pc->speed;
    s->output_applied    = false;
    s->move_active        = pc->is_move;
    s->spec              = pc->spec;
    s->end_limit         = MOTOR_LIMIT_POS;
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
    s->phase             = MOTOR_PHASE_RUNNING;
}

static bool reversal(motor_executor_t *e, int i, const motor_pending_cmd_t *pc)
{
    motor_mstate_t *s = &e->m[i];

    if (!immediate_cut(e, i)) {
        enter_fault(e, i, MOTOR_FAULT_DRIVER_PORT_FATAL);
        return false;
    }
    s->after_reversal = *pc;
    s->reversal_until = e->now + e->cfg.motors[i].reversal_stop_ms;
    s->phase          = MOTOR_PHASE_REVERSAL_WAIT;
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

static motor_cmd_result_t after_begin_start(motor_executor_t *e, int i, const char *started_reason)
{
    motor_phase_t phase = e->m[i].phase;

    if (phase == MOTOR_PHASE_WAITING_START) {
        return cmd_make(MOTOR_CMD_QUEUED, "prepare");
    }
    if (phase == MOTOR_PHASE_FAULT) {
        return cmd_make(MOTOR_CMD_ACCEPTED, "prepare-failed");
    }
    return cmd_make(MOTOR_CMD_ACCEPTED, started_reason);
}

static motor_cmd_result_t enter_reversal(motor_executor_t *e, int i, const motor_pending_cmd_t *pc,
                                         const char *reason)
{
    if (!reversal(e, i, pc)) {
        return cmd_make(MOTOR_CMD_ACCEPTED, "cutoff-failed");
    }
    return cmd_make(MOTOR_CMD_ACCEPTED, reason);
}

/**
 * @brief  锁存运动目标并由内部状态机收敛；调用方不必按相位选命令
 */
static motor_cmd_result_t apply_goal(motor_executor_t *e, int i, const motor_pending_cmd_t *pc)
{
    motor_mstate_t *s = &e->m[i];

    switch (s->phase) {
    case MOTOR_PHASE_RUNNING:
        if ((pc->dir != s->dir) || (pc->speed.kind != s->speed.kind)) {
            return enter_reversal(e, i, pc, "reversal");
        }
        apply_goal_in_place(s, pc);
        return cmd_make(MOTOR_CMD_ACCEPTED, "goal-updated");

    case MOTOR_PHASE_DECELERATING:
        if ((pc->dir != s->dir) || (pc->speed.kind != s->speed.kind)) {
            return enter_reversal(e, i, pc, "output-mode-change");
        }
        begin_start(e, i, pc);
        return after_begin_start(e, i, "restart");

    case MOTOR_PHASE_WAITING_START:
        s->pending = *pc;
        s->queued  = true;
        return cmd_make(MOTOR_CMD_QUEUED, "pending-updated");

    case MOTOR_PHASE_REVERSAL_WAIT:
        s->after_reversal = *pc;
        return cmd_make(MOTOR_CMD_QUEUED, "reversal-goal-updated");

    case MOTOR_PHASE_STOPPED:
        if (!interlock_ok(e, i)) {
            return cmd_reject("interlock");
        }
        if (in_cooldown(e, i)) {
            s->pending = *pc;
            s->queued  = true;
            s->phase   = MOTOR_PHASE_WAITING_START;
            return cmd_make(MOTOR_CMD_QUEUED, "cooldown");
        }
        begin_start(e, i, pc);
        return after_begin_start(e, i, "start");

    default:
        return cmd_reject("bad-phase");
    }
}

/* ------------------------- 故障 ------------------------- */

static void fault_one(motor_executor_t *e, int i, motor_fault_code_t code, bool fatal)
{
    motor_mstate_t *s = &e->m[i];
    (void)immediate_cut(e, i);
    /* 结算耗时后再转入故障态，故障事件才能带上已运行时长。 */
    settle_elapsed(e, i);
    s->phase             = MOTOR_PHASE_FAULT;
    s->fatal             = fatal;
    s->fault_code        = code;
    s->move_active        = false;
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
        if (e->m[j].phase == MOTOR_PHASE_FAULT || e->m[j].phase == MOTOR_PHASE_ESTOP) {
            continue;
        }
        fault_one(e, j, MOTOR_FAULT_SHARED_DRIVER, false);
        push_event(e, j, MOTOR_EVENT_FAULT, MOTOR_END_NONE, MOTOR_FAULT_SHARED_DRIVER, MOTOR_LEVEL_FAULT);
    }
}

static void enter_fault(motor_executor_t *e, int i, motor_fault_code_t code)
{
    fault_one(e, i, code, false);
    push_event(e, i, MOTOR_EVENT_FAULT, MOTOR_END_NONE, code, MOTOR_LEVEL_FAULT);
    link_shared_driver(e, i);
}

static void enter_fatal(motor_executor_t *e, int i, motor_fault_code_t code)
{
    fault_one(e, i, code, true);
    push_event(e, i, MOTOR_EVENT_FAULT, MOTOR_END_NONE, code, MOTOR_LEVEL_FATAL);
}

static void warn(motor_executor_t *e, int i, motor_fault_code_t code)
{
    if (e->cfg.motors[i].enc_escalate && code == MOTOR_FAULT_ENCODER_SIGNAL) {
        enter_fault(e, i, code);
        return;
    }
    push_event(e, i, MOTOR_EVENT_WARNING, MOTOR_END_NONE, code, MOTOR_LEVEL_WARNING);
}

/* ------------------------- 停止 / 到位 ------------------------- */

static bool zero_encoder_baseline(motor_executor_t *e, int i);

static void finish_halt(motor_executor_t *e, int i)
{
    motor_mstate_t *s = &e->m[i];
    if (!immediate_cut(e, i)) {
        enter_fault(e, i, MOTOR_FAULT_DRIVER_PORT_FATAL);
        return;
    }
    /* 置 STOPPED 前先结算耗时，否则随后的停止事件只能读到未累加的 0。 */
    settle_elapsed(e, i);
    /* ORIGIN 限位运动被外部停止时，只要机构确实压在原点就必须重建基准：清零依据是
     * 机构位置，与运动因何结束、是否经 motor_home 无关。上层可能与本执行器同拍
     * 看到原点限位并先下发停止，此时走 finish_halt 而非 complete_move。 */
    if (motor_limit_mask_has(s->spec.limit_mask, MOTOR_LIMIT_ORIGIN)
        && sensor_limit(e, i, MOTOR_LIMIT_ORIGIN)) {
        (void)zero_encoder_baseline(e, i);
    }
    s->phase          = MOTOR_PHASE_STOPPED;
    s->move_active     = false;
    s->cooldown_until = e->now + e->cfg.motors[i].cooldown_ms;
    if (s->emit_stop_on_halt) {
        s->emit_stop_on_halt = false;
        push_event(e, i, MOTOR_EVENT_STOPPED, MOTOR_END_NONE, MOTOR_FAULT_NONE, MOTOR_LEVEL_WARNING);
    }
}

/**
 * @brief  到达原点后建立可信基准（有编码器时清零）
 * @note   无编码器轴无位置基准概念，直接标可信。
 * @note   绝对编码器：限位只作停机，不改写 position（真值始终来自传感器采样）。
 */
static bool zero_encoder_baseline(motor_executor_t *e, int i)
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


static void complete_move(motor_executor_t *e, int i, motor_event_type_t type, motor_end_condition_t trig)
{
    motor_mstate_t     *s   = &e->m[i];
    motor_fault_level_t lvl = (type == MOTOR_EVENT_ARRIVED) ? MOTOR_LEVEL_WARNING : MOTOR_LEVEL_FAULT;
    /* 清零依据是「监视 ORIGIN 且结束时机构确实压在原点」，与是否 motor_home、
     * 运动因何结束无关。上层可能同拍先下发停止（END_NONE），仍须重建基准。 */
    bool origin_reached = motor_limit_mask_has(s->spec.limit_mask, MOTOR_LIMIT_ORIGIN)
                          && sensor_limit(e, i, MOTOR_LIMIT_ORIGIN);

    /* 先结算耗时，保留到位瞬间的时长供随后的事件读取。 */
    settle_elapsed(e, i);
    s->move_active        = false;
    s->emit_stop_on_halt = false;
    if (origin_reached) {
        finish_halt(e, i);
        if (s->phase == MOTOR_PHASE_FAULT) {
            return;
        }
        /* finish_halt 已尝试建基准；失败则进入故障（与旧 complete_move 路径一致）。 */
        if (!s->baseline_trusted) {
            enter_fault(e, i, MOTOR_FAULT_ENCODER_SIGNAL);
            return;
        }
        push_event(e, i, type, trig, MOTOR_FAULT_NONE, lvl);
        return;
    }

    /* 非原点动作保留到位瞬间的位置和耗时。 */
    push_event(e, i, type, trig, MOTOR_FAULT_NONE, lvl);
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
        static const motor_limit_kind_t k_order[] = {
            MOTOR_LIMIT_ORIGIN,
            MOTOR_LIMIT_POS,
            MOTOR_LIMIT_NEG,
        };
        unsigned n;

        for (n = 0; n < (sizeof(k_order) / sizeof(k_order[0])); ++n) {
            motor_limit_kind_t kind = k_order[n];
            if (motor_limit_mask_has(s->spec.limit_mask, kind) && sensor_limit(e, i, kind)) {
                s->end_limit = kind;
                complete_move(e, i, MOTOR_EVENT_ARRIVED, MOTOR_END_LIMIT);
                return;
            }
        }
    }
    if (s->spec.use_position) {
        int64_t target    = s->spec.target_pos;
        int     tolerance = mc->pos_tolerance;
        bool    reached   = (s->dir == MOTOR_DIR_FORWARD) ? (s->position >= lower_bound(target, tolerance))
                                                          : (s->position <= upper_bound(target, tolerance));

        if (reached) {
            complete_move(e, i, MOTOR_EVENT_ARRIVED, MOTOR_END_POSITION);
            return;
        }
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
    if (s->spec.use_current) {
        uint64_t run_ms = s->elapsed_ms + (e->now - s->move_start_ms);
        if (run_ms < (uint64_t)s->spec.current_blank_ms) {
            s->cur_stop_ms = 0;
        } else {
            int cur = drv_current(motor_drv(e, i));
            if (cur > s->spec.current_limit) {
                s->cur_stop_ms += (uint32_t)e->cfg.tick_ms;
                if (s->cur_stop_ms >= s->spec.current_confirm_ms) {
                    complete_move(e, i, MOTOR_EVENT_ARRIVED, MOTOR_END_CURRENT);
                    return;
                }
            } else {
                s->cur_stop_ms = 0;
            }
        }
    }
    uint64_t el = s->elapsed_ms + (e->now - s->move_start_ms);
    if (s->spec.use_time && el >= s->spec.duration_ms) {
        complete_move(e, i, MOTOR_EVENT_ARRIVED, MOTOR_END_TIME);
        return;
    }
    uint64_t max_t = s->spec.max_time_ms ? s->spec.max_time_ms : (uint64_t)mc->default_max_move_ms;
    if (el >= max_t) {
        complete_move(e, i, MOTOR_EVENT_TIMEOUT, MOTOR_END_TIMEOUT);
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
    s->position += (s->dir == MOTOR_DIR_FORWARD) ? d : -d;
    ad = motor_iabs64(d);
    if (mc->enc_stall_ticks > 0) {
        if (ad == 0) {
            if (++s->enc_stall >= mc->enc_stall_ticks && !s->enc_warned) {
                s->enc_warned  = true;
                s->enc_healthy = false;
                warn(e, i, MOTOR_FAULT_ENCODER_SIGNAL);
            }
        } else {
            s->enc_stall = 0;
        }
    }
    if (mc->enc_jump_max > 0 && ad > mc->enc_jump_max && !s->enc_warned) {
        s->enc_warned  = true;
        s->enc_healthy = false;
        warn(e, i, MOTOR_FAULT_ENCODER_SIGNAL);
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

static void trigger_estop(motor_executor_t *e)
{
    for (int i = 0; i < e->motor_count; ++i) {
        motor_mstate_t *s          = &e->m[i];
        bool            was_active = (s->phase != MOTOR_PHASE_STOPPED && s->phase != MOTOR_PHASE_FAULT);
        (void)immediate_cut(e, i);
        /* 结算耗时后再转入急停态，急停事件才能带上被切断时的已运行时长。 */
        settle_elapsed(e, i);
        s->queued     = false;
        s->move_active = false;
        s->phase      = MOTOR_PHASE_ESTOP;
        if (was_active) {
            push_event(e, i, MOTOR_EVENT_ESTOP, MOTOR_END_NONE, MOTOR_FAULT_NONE, MOTOR_LEVEL_FAULT);
        }
    }
    e->estop_latched = true;
}

static void trigger_safe(motor_executor_t *e)
{
    for (int i = 0; i < e->motor_count; ++i) {
        bool was_active = (e->m[i].phase != MOTOR_PHASE_STOPPED && e->m[i].phase != MOTOR_PHASE_FAULT
                           && e->m[i].phase != MOTOR_PHASE_ESTOP);
        (void)immediate_cut(e, i);
        e->m[i].queued     = false;
        e->m[i].move_active = false;
        fault_one(e, i, MOTOR_FAULT_WATCHDOG, false);
        if (was_active) {
            push_event(e, i, MOTOR_EVENT_FAULT, MOTOR_END_NONE, MOTOR_FAULT_WATCHDOG, MOTOR_LEVEL_FAULT);
        }
    }
    e->safe_latched = true;
}

/* ------------------------- 事件分发 ------------------------- */

static void dispatch(motor_executor_t *e)
{
    if (!e->cb) {
        return;
    }
    e->in_dispatch = true;
    motor_event_t ev;
    while (e->ev_count > 0) {
        ev         = e->events[e->ev_head];
        e->ev_head = (e->ev_head + 1) % MOTOR_EVENT_QUEUE_CAP;
        e->ev_count--;
        e->cb(&ev, e->cb_ctx);
    }
    e->in_dispatch = false;
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
            if (drv_poll(motor_drv(e, i), i) == MOTOR_PREPARE_FAILED) {
                enter_fault(e, i, MOTOR_FAULT_PREPARE_FAILED);
                break;
            }
            update_encoder(e, i, true);
            check_end(e, i);
            if (s->phase == MOTOR_PHASE_RUNNING) {
                if (!apply_output(e, i)) {
                    enter_fault(e, i, MOTOR_FAULT_DRIVER_PORT_FATAL);
                    break;
                }
                monitor(e, i);
            }
            break;
        case MOTOR_PHASE_DECELERATING:
            update_encoder(e, i, true);
            finish_halt(e, i);
            break;
        default:
            update_encoder(e, i, false);
            break;
        }
    }
    dispatch(e);
}

/* ------------------------- 初始化 ------------------------- */

static motor_init_result_t init_ok(void)
{
    motor_init_result_t r;
    r.ok    = true;
    r.error = "";
    return r;
}

static motor_init_result_t init_err(const char *msg)
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
        if (mc->default_max_move_ms <= 0) {
            return init_err("defaultMaxMoveMs must be > 0");
        }
        if (mc->cooldown_ms < 0 || mc->reversal_stop_ms < 0 || mc->accel_ms < 0
            || mc->pos_tolerance < 0 || mc->decel_point < 0) {
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
        e->m[i].phase        = MOTOR_PHASE_STOPPED;
        e->m[i].dir          = MOTOR_DIR_FORWARD;
        e->m[i].fault_code   = MOTOR_FAULT_NONE;
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
    e->initialized = true;
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

/* ------------------------- 命令 ------------------------- */

enum {
    MOTOR_CMD_NEED_NOW = 1u << 0,       /**< 刷新 e->now */
    MOTOR_CMD_REJECT_SAFETY = 1u << 1,  /**< 急停/看门狗锁定时拒绝 */
    MOTOR_CMD_REJECT_FAULT = 1u << 2    /**< FAULT 相位时拒绝 */
};

static bool bad_motor(motor_executor_t *e, int i)
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

motor_cmd_result_t motor_run(motor_executor_t        *e,
                             int                      i,
                             motor_speed_t            spd,
                             motor_direction_t        dir,
                             const motor_move_spec_t *spec)
{
    motor_cmd_result_t  g = cmd_guard(e, i, MOTOR_CMD_NEED_NOW | MOTOR_CMD_REJECT_SAFETY | MOTOR_CMD_REJECT_FAULT);
    motor_pending_cmd_t pc = {0};

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
                dispatch(e);
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
    int                freq;

    if (!motor_cmd_ok(g)) {
        return g;
    }
    if (!e->cfg.motors[i].has_encoder) {
        return cmd_reject("no-encoder");
    }
    freq = e->cfg.motors[i].slow_freq > 0 ? e->cfg.motors[i].slow_freq : MOTOR_HOME_DEFAULT_FREQ_CENTI_HZ;
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
    push_event(e, i, MOTOR_EVENT_WARNING, MOTOR_END_NONE, MOTOR_FAULT_ENCODER_SIGNAL, MOTOR_LEVEL_WARNING);
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

motor_cmd_result_t motor_recover(motor_executor_t *e, int i, motor_recovery_step_t step)
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
            push_event(e, i, MOTOR_EVENT_FAULT, MOTOR_END_NONE, s->fault_code, MOTOR_LEVEL_FAULT);
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

motor_phase_t motor_phase(const motor_executor_t *e, int i)
{
    return e->m[i].phase;
}

int64_t motor_position(const motor_executor_t *e, int i)
{
    return e->m[i].position;
}

int motor_current_freq(const motor_executor_t *e, int i)
{
    const motor_mstate_t *state = &e->m[i];

    if ((state->phase != MOTOR_PHASE_RUNNING) || (state->speed.kind != MOTOR_SPEED_FREQ)) {
        return 0;
    }
    return state->speed.value;
}

motor_direction_t motor_direction(const motor_executor_t *e, int i)
{
    return e->m[i].dir;
}

motor_fault_code_t motor_fault_code(const motor_executor_t *e, int i)
{
    return e->m[i].fault_code;
}

bool motor_baseline_trusted(const motor_executor_t *e, int i)
{
    /* 无编码器机构恒可信，与端口契约及 encoder_healthy 对称。 */
    if (!e->cfg.motors[i].has_encoder) {
        return true;
    }
    return e->m[i].baseline_trusted;
}

bool motor_encoder_healthy(const motor_executor_t *e, int i)
{
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

/* ------------------------- 事件 ------------------------- */

void motor_set_event_callback(motor_executor_t *e, motor_event_cb_t cb, void *ctx)
{
    if (e->in_dispatch) {
        return;
    }
    e->cb     = cb;
    e->cb_ctx = ctx;
}

bool motor_pop_event(motor_executor_t *e, motor_event_t *out)
{
    if (e->ev_count <= 0) {
        return false;
    }
    *out       = e->events[e->ev_head];
    e->ev_head = (e->ev_head + 1) % MOTOR_EVENT_QUEUE_CAP;
    e->ev_count--;
    return true;
}

bool motor_pop_event_for(motor_executor_t *e, int motor, motor_event_t *out)
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

/* ------------------------- hal_motor_exec_port ------------------------- */

static motor_direction_t to_exec_dir(hal_motor_dir_t dir)
{
    return (dir == HAL_MOTOR_DIR_REVERSE) ? MOTOR_DIR_REVERSE : MOTOR_DIR_FORWARD;
}

static hal_motor_dir_t from_exec_dir(motor_direction_t dir)
{
    return (dir == MOTOR_DIR_REVERSE) ? HAL_MOTOR_DIR_REVERSE : HAL_MOTOR_DIR_FORWARD;
}

static hal_motor_phase_t from_exec_phase(motor_phase_t ph)
{
    switch (ph) {
    case MOTOR_PHASE_WAITING_START:
        return HAL_MOTOR_PHASE_WAITING_START;
    case MOTOR_PHASE_REVERSAL_WAIT:
        return HAL_MOTOR_PHASE_REVERSAL_WAIT;
    case MOTOR_PHASE_RUNNING:
        return HAL_MOTOR_PHASE_RUNNING;
    case MOTOR_PHASE_DECELERATING:
        return HAL_MOTOR_PHASE_DECELERATING;
    case MOTOR_PHASE_FAULT:
        return HAL_MOTOR_PHASE_FAULT;
    case MOTOR_PHASE_ESTOP:
        return HAL_MOTOR_PHASE_ESTOP;
    case MOTOR_PHASE_STOPPED:
    default:
        return HAL_MOTOR_PHASE_STOPPED;
    }
}

static hal_motor_fault_code_t from_exec_fault(motor_fault_code_t fc)
{
    switch (fc) {
    case MOTOR_FAULT_OVERCURRENT:
        return HAL_MOTOR_FAULT_OVERCURRENT;
    case MOTOR_FAULT_UNDERCURRENT:
        return HAL_MOTOR_FAULT_UNDERCURRENT;
    case MOTOR_FAULT_DRIVER_FEEDBACK:
        return HAL_MOTOR_FAULT_DRIVER_FEEDBACK;
    case MOTOR_FAULT_OVERTEMP:
        return HAL_MOTOR_FAULT_OVERTEMP;
    case MOTOR_FAULT_UNDERVOLTAGE:
        return HAL_MOTOR_FAULT_UNDERVOLTAGE;
    case MOTOR_FAULT_PREPARE_FAILED:
        return HAL_MOTOR_FAULT_PREPARE_FAILED;
    case MOTOR_FAULT_ENCODER_SIGNAL:
        return HAL_MOTOR_FAULT_ENCODER_SIGNAL;
    case MOTOR_FAULT_WATCHDOG:
        return HAL_MOTOR_FAULT_WATCHDOG;
    case MOTOR_FAULT_DRIVER_PORT_FATAL:
        return HAL_MOTOR_FAULT_DRIVER_PORT_FATAL;
    case MOTOR_FAULT_SHARED_DRIVER:
        return HAL_MOTOR_FAULT_SHARED_DRIVER;
    case MOTOR_FAULT_NONE:
    default:
        return HAL_MOTOR_FAULT_NONE;
    }
}

static hal_motor_end_condition_t from_exec_end_condition(motor_end_condition_t trig)
{
    switch (trig) {
    case MOTOR_END_LIMIT:
        return HAL_MOTOR_END_LIMIT;
    case MOTOR_END_POSITION:
        return HAL_MOTOR_END_POSITION;
    case MOTOR_END_SOFT_LIMIT:
        return HAL_MOTOR_END_SOFT_LIMIT;
    case MOTOR_END_CURRENT:
        return HAL_MOTOR_END_CURRENT;
    case MOTOR_END_TIME:
        return HAL_MOTOR_END_TIME;
    case MOTOR_END_TIMEOUT:
        return HAL_MOTOR_END_TIMEOUT;
    case MOTOR_END_NONE:
    default:
        return HAL_MOTOR_END_NONE;
    }
}

static hal_motor_limit_kind_t from_exec_limit(motor_limit_kind_t kind)
{
    switch (kind) {
    case MOTOR_LIMIT_NEG:
        return HAL_MOTOR_LIMIT_NEG;
    case MOTOR_LIMIT_ORIGIN:
        return HAL_MOTOR_LIMIT_ORIGIN;
    case MOTOR_LIMIT_POS:
    default:
        return HAL_MOTOR_LIMIT_POS;
    }
}

static hal_motor_event_type_t from_exec_event_type(motor_event_type_t type)
{
    switch (type) {
    case MOTOR_EVENT_TIMEOUT:
        return HAL_MOTOR_EVENT_TIMEOUT;
    case MOTOR_EVENT_STOPPED:
        return HAL_MOTOR_EVENT_STOPPED;
    case MOTOR_EVENT_FAULT:
        return HAL_MOTOR_EVENT_FAULT;
    case MOTOR_EVENT_ESTOP:
        return HAL_MOTOR_EVENT_ESTOP;
    case MOTOR_EVENT_WARNING:
        return HAL_MOTOR_EVENT_WARNING;
    case MOTOR_EVENT_ARRIVED:
    default:
        return HAL_MOTOR_EVENT_ARRIVED;
    }
}

static motor_recovery_step_t to_exec_recovery_step(hal_motor_recovery_step_t step)
{
    return (step == HAL_MOTOR_RECOVERY_MODULE_STOP) ? MOTOR_RECOVERY_MODULE_STOP : MOTOR_RECOVERY_DRIVER_RESET;
}

static motor_speed_t to_exec_speed(hal_motor_speed_t spd)
{
    return (spd.kind == HAL_MOTOR_SPEED_GEAR) ? motor_speed_gear(spd.value) : motor_speed_freq(spd.value);
}

static void to_exec_move_spec(const hal_motor_move_spec_t *src, motor_move_spec_t *dst)
{
    dst->limit_mask          = src->limit_mask;
    dst->use_position        = src->use_position;
    dst->target_pos          = src->target_pos;
    dst->use_soft_limit      = src->use_soft_limit;
    dst->use_current         = src->use_current;
    dst->current_limit       = src->current_limit;
    dst->current_confirm_ms  = src->current_confirm_ms;
    dst->current_blank_ms    = src->current_blank_ms;
    dst->use_time            = src->use_time;
    dst->duration_ms         = src->duration_ms;
    dst->max_time_ms         = src->max_time_ms;
}

static hal_motor_cmd_result_t from_exec_result(motor_cmd_result_t r)
{
    hal_motor_cmd_result_t out;

    switch (r.status) {
    case MOTOR_CMD_QUEUED:
        out.status = HAL_MOTOR_CMD_QUEUED;
        break;
    case MOTOR_CMD_REJECTED:
        out.status = HAL_MOTOR_CMD_REJECTED;
        break;
    case MOTOR_CMD_ACCEPTED:
    default:
        out.status = HAL_MOTOR_CMD_ACCEPTED;
        break;
    }
    out.reason = r.reason;
    return out;
}

hal_motor_cmd_result_t hal_motor_run(hal_motor_exec_t            *exec,
                                     int                          motor,
                                     hal_motor_speed_t            spd,
                                     hal_motor_dir_t              dir,
                                     const hal_motor_move_spec_t *spec)
{
    motor_move_spec_t        exec_spec;
    const motor_move_spec_t *exec_spec_p = NULL;

    if (spec != NULL) {
        to_exec_move_spec(spec, &exec_spec);
        exec_spec_p = &exec_spec;
    }

    return from_exec_result(
        motor_run((motor_executor_t *)exec, motor, to_exec_speed(spd), to_exec_dir(dir), exec_spec_p));
}

hal_motor_cmd_result_t hal_motor_stop(hal_motor_exec_t *exec, int motor)
{
    return from_exec_result(motor_stop((motor_executor_t *)exec, motor));
}

hal_motor_cmd_result_t hal_motor_home(hal_motor_exec_t *exec, int motor)
{
    return from_exec_result(motor_home((motor_executor_t *)exec, motor));
}

hal_motor_cmd_result_t hal_motor_recover(hal_motor_exec_t *exec, int motor, hal_motor_recovery_step_t step)
{
    return from_exec_result(motor_recover((motor_executor_t *)exec, motor, to_exec_recovery_step(step)));
}

hal_motor_phase_t hal_motor_phase(const hal_motor_exec_t *exec, int motor)
{
    return from_exec_phase(motor_phase((const motor_executor_t *)exec, motor));
}

int64_t hal_motor_position(const hal_motor_exec_t *exec, int motor)
{
    return motor_position((const motor_executor_t *)exec, motor);
}

hal_motor_dir_t hal_motor_direction(const hal_motor_exec_t *exec, int motor)
{
    return from_exec_dir(motor_direction((const motor_executor_t *)exec, motor));
}

hal_motor_fault_code_t hal_motor_fault_code(const hal_motor_exec_t *exec, int motor)
{
    return from_exec_fault(motor_fault_code((const motor_executor_t *)exec, motor));
}

bool hal_motor_encoder_healthy(const hal_motor_exec_t *exec, int motor)
{
    return motor_encoder_healthy((const motor_executor_t *)exec, motor);
}

bool hal_motor_baseline_trusted(const hal_motor_exec_t *exec, int motor)
{
    return motor_baseline_trusted((const motor_executor_t *)exec, motor);
}

static void fill_hal_event(hal_motor_event_t *out, const motor_event_t *ev)
{
    out->motor      = ev->motor;
    out->type       = from_exec_event_type(ev->type);
    out->trigger    = from_exec_end_condition(ev->trigger);
    out->has_limit  = ev->has_limit;
    out->limit      = from_exec_limit(ev->limit);
    out->final_pos  = ev->final_pos;
    out->elapsed_ms = ev->elapsed_ms;
    out->fault      = from_exec_fault(ev->fault);
}

bool hal_motor_pop_event(hal_motor_exec_t *exec, hal_motor_event_t *out)
{
    motor_event_t ev;

    if (out == NULL) {
        return false;
    }
    if (!motor_pop_event((motor_executor_t *)exec, &ev)) {
        return false;
    }
    fill_hal_event(out, &ev);
    return true;
}

bool hal_motor_pop_event_for(hal_motor_exec_t *exec, int motor, hal_motor_event_t *out)
{
    motor_event_t ev;

    if (out == NULL) {
        return false;
    }
    if (!motor_pop_event_for((motor_executor_t *)exec, motor, &ev)) {
        return false;
    }
    fill_hal_event(out, &ev);
    return true;
}
