/**
 * @file    motor_tick_internal.c
 * @brief   motor 模块内部周期线程实现
 * @author  HUWANGWEI
 * @date    2026-04-14
 *
 * @note    本文件只负责编排周期线程各阶段，不承载具体编码器/驱动器监测业务细节。
 */

#include "common/log.h"
#include "common/time_util.h"
#include "domain/device/actuator/motor/motor_internal.h"

#include <pthread.h>
#include <string.h>
#include <unistd.h>

#define MOTOR_TICK_STACK_SIZE (16U * 1024U)

typedef struct {
    encoder_hw_job_t    encoder_jobs[MOTOR_ID_MAX];
    motor_monitor_job_t monitor_jobs[MOTOR_MON_CH_MAX];
    int                 done_ids[MOTOR_ID_MAX];
    sw_err_t            done_results[MOTOR_ID_MAX];
    int                 done_count;
    /* PENDING 延迟到期的电机（在锁外调 pre_start 回调，再在锁内应用启动）*/
    int pending_ids[MOTOR_ID_MAX];
    int pending_start_args[MOTOR_ID_MAX]; /* 频率=速度参考，挡位=带符号挡位 */
    int pending_count;
} motor_tick_ctx_t;

/** 从 pending 启动请求提取 pre_start 回调第二参数。 */
static int motor_pre_start_arg_from_req(const motor_start_req_t *req)
{
    if (req == NULL) {
        return 0;
    }
    return req->is_gear ? (int)req->gear : req->freq_ref;
}

static void motor_record_move_done_locked(motor_tick_ctx_t *tick, int id, sw_err_t result)
{
    if ((tick == NULL) || (tick->done_count >= MOTOR_ID_MAX)) {
        return;
    }

    if (motor_stop_and_mark_done_locked(id, result)) {
        tick->done_ids[tick->done_count]     = id;
        tick->done_results[tick->done_count] = result;
        tick->done_count++;
    }
}

static void motor_tick_reset(motor_tick_ctx_t *tick)
{
    if (tick == NULL) {
        return;
    }

    memset(tick, 0, sizeof(*tick));
}

static void motor_tick_identify_pending_locked(motor_tick_ctx_t *tick, uint32_t now_ms)
{
    for (int id = 0; id < MOTOR_ID_MAX; id++) {
        const motor_cfg_t *cfg = motor_get_cfg_locked(id);
        const motor_ctx_t *ctx = &s_ctx[id];
        bool               ready;

        if ((cfg == NULL) || (ctx->state != MOTOR_STATE_PENDING)) {
            continue;
        }

        /* last_output_off_ms == 0 表示从未运行过，延迟视为已满足 */
        ready = (ctx->last_output_off_ms == 0U) ||
                (time_elapsed_ms(ctx->last_output_off_ms, now_ms) >= cfg->post_stop_delay_ms);
        if (!ready) {
            continue;
        }

        if (tick->pending_count < MOTOR_ID_MAX) {
            tick->pending_ids[tick->pending_count]        = id;
            tick->pending_start_args[tick->pending_count] = motor_pre_start_arg_from_req(&ctx->pending_start);
            tick->pending_count++;
        }
    }
}

static bool motor_tick_prepare_locked(motor_tick_ctx_t *tick, const hal_motor_ops_t *ops, uint32_t now_ms)
{
    if ((tick == NULL) || !s_initialized) {
        return false;
    }

    for (int id = 0; id < MOTOR_ID_MAX; id++) {
        const motor_cfg_t *cfg = motor_get_cfg_locked(id);
        motor_ctx_t       *ctx;

        if ((cfg == NULL) || (ops == NULL)) {
            continue;
        }

        ctx = &s_ctx[id];
        motor_encoder_update_locked(cfg, ctx, now_ms);
        motor_encoder_schedule_hw_job_locked(tick->encoder_jobs, id, cfg, ctx);
        motor_monitor_schedule_job_locked(tick->monitor_jobs, cfg, ctx);
    }

    motor_tick_identify_pending_locked(tick, now_ms);
    return true;
}

static void motor_tick_execute_encoder_jobs(motor_tick_ctx_t *tick, const hal_motor_ops_t *ops)
{
    if (tick == NULL) {
        return;
    }

    for (int id = 0; id < MOTOR_ID_MAX; id++) {
        motor_encoder_execute_hw_job(ops, id, &tick->encoder_jobs[id]);
    }
}

static bool motor_tick_finalize_encoder_locked(motor_tick_ctx_t *tick, const hal_motor_ops_t *ops)
{
    if ((tick == NULL) || !s_initialized) {
        return false;
    }

    for (int id = 0; id < MOTOR_ID_MAX; id++) {
        const motor_cfg_t *cfg = motor_get_cfg_locked(id);
        motor_ctx_t       *ctx;

        if ((cfg == NULL) || (ops == NULL)) {
            continue;
        }

        ctx = &s_ctx[id];
        motor_encoder_finalize_locked(cfg, ctx, time_util_get_ms(), &tick->encoder_jobs[id]);
    }

    return true;
}

static bool motor_tick_is_limit_hit(const hal_motor_ops_t *ops, int id, int run_cmd)
{
    if ((ops == NULL) || (run_cmd == 0)) {
        return false;
    }

    if (run_cmd > 0) {
        return (ops->at_fwd_limit != NULL) && ops->at_fwd_limit(id);
    }

    return (ops->at_rev_limit != NULL) && ops->at_rev_limit(id);
}

static bool motor_tick_handle_timeout_locked(motor_tick_ctx_t  *tick,
                                             int                id,
                                             const motor_cfg_t *cfg,
                                             uint32_t           elapsed_ms)
{
    if ((tick == NULL) || (cfg == NULL) || (cfg->timeout_ms == MOTOR_TIMEOUT_FOREVER) ||
        (elapsed_ms < cfg->timeout_ms)) {
        return false;
    }

    LOG_ERROR("motor[%s]: timeout after %u ms", cfg->name, (unsigned)cfg->timeout_ms);
    motor_record_move_done_locked(tick, id, SW_ERR_TIMEOUT);
    return true;
}

static bool motor_tick_collect_done_locked(motor_tick_ctx_t *tick, const hal_motor_ops_t *ops)
{
    if ((tick == NULL) || !s_initialized) {
        return false;
    }

    for (int id = 0; id < MOTOR_ID_MAX; id++) {
        const motor_cfg_t *cfg = motor_get_cfg_locked(id);
        motor_ctx_t       *ctx;
        uint32_t           elapsed_ms;

        if ((cfg == NULL) || (ops == NULL)) {
            continue;
        }

        ctx = &s_ctx[id];
        if (!motor_is_move_state(ctx->state)) {
            continue;
        }

        elapsed_ms = motor_calc_elapsed_ms_locked(ctx, time_util_get_ms());

        if (motor_tick_is_limit_hit(ops, id, ctx->run_cmd)) {
            LOG_INFO("motor[%s]: limit reached", cfg->name);
            motor_record_move_done_locked(tick, id, SW_OK);
            continue;
        }

        if (motor_tick_handle_timeout_locked(tick, id, cfg, elapsed_ms)) {
            continue;
        }
    }

    return true;
}

/* 在锁外调用 pre_start 回调（避免回调内部持锁导致死锁）*/
static void motor_tick_call_pre_start_cbs(const motor_tick_ctx_t *tick)
{
    if (tick == NULL) {
        return;
    }

    for (int i = 0; i < tick->pending_count; i++) {
        int id = tick->pending_ids[i];

        if ((id >= 0) && (id < MOTOR_ID_MAX) && (s_pre_start_fns[id] != NULL)) {
            (void)s_pre_start_fns[id](id, tick->pending_start_args[i], s_pre_start_ctxs[id]);
        }
    }
}

/* 在锁内应用 PENDING → HOLD/MOVE 启动（回调已在锁外完成）*/
static void motor_tick_apply_pending_starts_locked(const motor_tick_ctx_t *tick, uint32_t now_ms)
{
    if (tick == NULL) {
        return;
    }

    for (int i = 0; i < tick->pending_count; i++) {
        int id = tick->pending_ids[i];

        if ((id < 0) || (id >= MOTOR_ID_MAX)) {
            continue;
        }
        /* 重新检查：锁外期间可能已被 motor_stop() 取消 */
        if (s_ctx[id].state != MOTOR_STATE_PENDING) {
            continue;
        }

        (void)motor_apply_pending_start_locked(id, now_ms);
    }
}

static void motor_tick_notify_done(const motor_tick_ctx_t *tick)
{
    if (tick == NULL) {
        return;
    }

    for (int i = 0; i < tick->done_count; i++) {
        motor_notify_done(tick->done_ids[i], tick->done_results[i]);
    }
}

/* motor_tick_loop 每 10ms 执行一次，分两个加锁区：
 *
 *  ┌─ 加锁 ──────────────────────────────────────────────────────────────────┐
 *  │  prepare：读编码器软状态 / 调度 IO 任务 / 识别 PENDING 已到期的电机      │
 *  └─ 解锁 ──────────────────────────────────────────────────────────────────┘
 *       ↓（无锁，耗时 IO 操作）
 *  执行编码器 HW 读写 / 驱动器电流与运行状态采样 / pre_start 回调（接触器切换等）
 *       ↓
 *  ┌─ 加锁 ──────────────────────────────────────────────────────────────────┐
 *  │  写回编码器结果 → 应用驱动器故障 → 启动 PENDING 电机 → 收集完成事件     │
 *  └─ 解锁 ──────────────────────────────────────────────────────────────────┘
 *       ↓（无锁）
 *  触发完成回调（motor_done_cb_t）
 *
 *  两次加锁的原因：HW IO（编码器读写 / 驱动器采样、pre_start 回调）必须在无锁下执行，
 *  避免阻塞其他调用方；状态变更（写回、故障、启动、完成）则必须在锁内保证原子性。
 */
static void *motor_tick_loop(void *arg)
{
    (void)arg;

    while (true) {
        motor_tick_ctx_t       tick;
        uint32_t               now_ms = time_util_get_ms();
        const hal_motor_ops_t *ops    = hal_motor_get_ops();

        motor_tick_reset(&tick);

        /* ── 加锁区一：读状态、调度任务、识别 PENDING ── */
        pthread_mutex_lock(&s_mutex);
        if (!motor_tick_prepare_locked(&tick, ops, now_ms)) {
            pthread_mutex_unlock(&s_mutex);
            usleep((unsigned long)MOTOR_TICK_PERIOD_MS * 1000UL);
            continue;
        }
        pthread_mutex_unlock(&s_mutex);

        /* ── 无锁区：执行 IO 操作（可阻塞，不持锁）── */
        motor_tick_execute_encoder_jobs(&tick, ops);
        motor_monitor_collect_samples(tick.monitor_jobs, ops);
        motor_tick_call_pre_start_cbs(&tick);

        /* ── 加锁区二：写回结果、应用故障、启动 PENDING、收集完成 ── */
        pthread_mutex_lock(&s_mutex);
        if (!motor_tick_finalize_encoder_locked(&tick, ops) || !s_initialized) {
            pthread_mutex_unlock(&s_mutex);
            usleep((unsigned long)MOTOR_TICK_PERIOD_MS * 1000UL);
            continue;
        }
        motor_monitor_apply_faults_locked(tick.monitor_jobs, ops);
        motor_tick_apply_pending_starts_locked(&tick, now_ms);
        if (!motor_tick_collect_done_locked(&tick, ops)) {
            pthread_mutex_unlock(&s_mutex);
            usleep((unsigned long)MOTOR_TICK_PERIOD_MS * 1000UL);
            continue;
        }
        pthread_mutex_unlock(&s_mutex);

        /* ── 无锁区：触发完成回调 ── */
        motor_tick_notify_done(&tick);
        usleep((unsigned long)MOTOR_TICK_PERIOD_MS * 1000UL);
    }

    return NULL;
}

sw_err_t motor_tick_start(void)
{
    pthread_attr_t attr;
    pthread_t      tid;

    pthread_attr_init(&attr);
    pthread_attr_setstacksize(&attr, MOTOR_TICK_STACK_SIZE);

    if (pthread_create(&tid, &attr, motor_tick_loop, NULL) != 0) {
        pthread_attr_destroy(&attr);
        LOG_ERROR("motor_tick_start: pthread_create failed");
        return SW_ERR_HW;
    }

    pthread_attr_destroy(&attr);
    pthread_detach(tid);
    LOG_INFO("motor_tick_start: motor_tick thread started");
    return SW_OK;
}
