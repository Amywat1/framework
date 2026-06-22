/**
 * @file    motor_tick_internal.c
 * @brief   motor 模块内部周期线程实现
 * @author  HUWANGWEI
 * @date    2026-04-14
 *
 * @note    本文件只负责编排周期线程各阶段，不承载具体编码器/VFD 业务细节。
 */

#include "domain/device/actuator/motor/motor_internal.h"

#include "common/time_util.h"
#include "common/log.h"
#include "config/threading/thread_config.h"

#include <string.h>
#include <unistd.h>

typedef struct
{
    encoder_hw_job_t    encoder_jobs[MOTOR_ID_MAX];
    const motor_cfg_t  *encoder_cfgs[MOTOR_ID_MAX];
    motor_monitor_job_t monitor_jobs[MOTOR_MON_SRC_MAX];
    int                 done_ids[MOTOR_ID_MAX];
    sw_err_t            done_results[MOTOR_ID_MAX];
    int                 done_count;
} motor_tick_ctx_t;

static void motor_record_done_event_locked(motor_tick_ctx_t *tick, int id, sw_err_t result)
{
    if ((tick == NULL) || (tick->done_count >= MOTOR_ID_MAX))
    {
        return;
    }

    if (motor_finish_locked(id, result))
    {
        tick->done_ids[tick->done_count]     = id;
        tick->done_results[tick->done_count] = result;
        tick->done_count++;
    }
}

static void motor_tick_reset(motor_tick_ctx_t *tick)
{
    if (tick == NULL)
    {
        return;
    }

    memset(tick, 0, sizeof(*tick));
}

static bool motor_tick_prepare_locked(motor_tick_ctx_t      *tick,
                                      const hal_motor_ops_t *ops,
                                      uint32_t               now_ms)
{
    if ((tick == NULL) || !s_initialized)
    {
        return false;
    }

    for (int id = 0; id < MOTOR_ID_MAX; id++)
    {
        const motor_cfg_t *cfg = motor_get_cfg_locked(id);
        motor_ctx_t       *ctx;

        if ((cfg == NULL) || (ops == NULL))
        {
            continue;
        }

        ctx = &s_ctx[id];
        tick->encoder_cfgs[id] = cfg;
        motor_encoder_update_locked(id, ops, cfg, ctx, now_ms);
        motor_encoder_schedule_hw_job_locked(tick->encoder_jobs, ops, id, cfg, ctx);
        motor_monitor_schedule_job_locked(tick->monitor_jobs, cfg, ctx);
    }

    return true;
}

static void motor_tick_execute_encoder_jobs(motor_tick_ctx_t      *tick,
                                            const hal_motor_ops_t *ops)
{
    if (tick == NULL)
    {
        return;
    }

    for (int id = 0; id < MOTOR_ID_MAX; id++)
    {
        motor_encoder_execute_hw_job(ops, tick->encoder_cfgs[id], id, &tick->encoder_jobs[id]);
    }
}

static bool motor_tick_finalize_encoder_locked(motor_tick_ctx_t      *tick,
                                               const hal_motor_ops_t *ops)
{
    if ((tick == NULL) || !s_initialized)
    {
        return false;
    }

    for (int id = 0; id < MOTOR_ID_MAX; id++)
    {
        const motor_cfg_t *cfg = motor_get_cfg_locked(id);
        motor_ctx_t       *ctx;

        if ((cfg == NULL) || (ops == NULL))
        {
            continue;
        }

        ctx = &s_ctx[id];
        motor_encoder_finalize_locked(id,
                                      ops,
                                      cfg,
                                      ctx,
                                      time_util_get_ms(),
                                      &tick->encoder_jobs[id]);
    }

    return true;
}

static bool motor_tick_is_limit_hit(const hal_motor_ops_t *ops, int id, int speed_ref)
{
    if ((ops == NULL) || (speed_ref == 0))
    {
        return false;
    }

    if (speed_ref > 0)
    {
        return (ops->at_fwd_limit != NULL) && ops->at_fwd_limit(id);
    }

    return (ops->at_rev_limit != NULL) && ops->at_rev_limit(id);
}

static bool motor_tick_handle_target_reached_locked(motor_tick_ctx_t  *tick,
                                                    int                id,
                                                    const motor_cfg_t *cfg,
                                                    const motor_ctx_t *ctx)
{
    int32_t pos;
    bool    reached;

    if ((tick == NULL) || (cfg == NULL) || (ctx == NULL) ||
        (ctx->state != MOTOR_STATE_MOVE_POS) || !cfg->has_encoder)
    {
        return false;
    }

    pos = ctx->encoder_pos;
    reached = ((ctx->speed_ref > 0) && (pos >= ctx->target_pos)) ||
              ((ctx->speed_ref < 0) && (pos <= ctx->target_pos));
    if (!reached)
    {
        return false;
    }

    LOG_INFO("motor[%s]: position reached target=%d actual=%d",
             cfg->name, (int)ctx->target_pos, (int)pos);
    motor_record_done_event_locked(tick, id, SW_OK);
    return true;
}

static bool motor_tick_handle_move_time_locked(motor_tick_ctx_t  *tick,
                                               int                id,
                                               const motor_cfg_t *cfg,
                                               const motor_ctx_t *ctx,
                                               uint32_t           elapsed_ms)
{
    (void)id;

    if ((tick == NULL) || (cfg == NULL) || (ctx == NULL) ||
        (ctx->state != MOTOR_STATE_MOVE_TIME) ||
        (elapsed_ms < ctx->move_time_ms))
    {
        return false;
    }

    LOG_INFO("motor[%s]: move_time reached %u ms",
             cfg->name, (unsigned)ctx->move_time_ms);
    motor_record_done_event_locked(tick, id, SW_OK);
    return true;
}

static bool motor_tick_handle_timeout_locked(motor_tick_ctx_t  *tick,
                                             int                id,
                                             const motor_cfg_t *cfg,
                                             uint32_t           elapsed_ms)
{
    (void)id;

    if ((tick == NULL) || (cfg == NULL) ||
        (cfg->timeout_ms == MOTOR_TIMEOUT_FOREVER) ||
        (elapsed_ms < cfg->timeout_ms))
    {
        return false;
    }

    LOG_ERROR("motor[%s]: timeout after %u ms",
              cfg->name, (unsigned)cfg->timeout_ms);
    motor_record_done_event_locked(tick, id, SW_ERR_TIMEOUT);
    return true;
}

static bool motor_tick_collect_done_locked(motor_tick_ctx_t      *tick,
                                           const hal_motor_ops_t *ops)
{
    if ((tick == NULL) || !s_initialized)
    {
        return false;
    }

    for (int id = 0; id < MOTOR_ID_MAX; id++)
    {
        const motor_cfg_t *cfg = motor_get_cfg_locked(id);
        motor_ctx_t       *ctx;
        uint32_t           elapsed_ms;

        if ((cfg == NULL) || (ops == NULL))
        {
            continue;
        }

        ctx = &s_ctx[id];
        if (!motor_is_move_state(ctx->state))
        {
            continue;
        }

        elapsed_ms = motor_calc_elapsed_ms_locked(ctx, time_util_get_ms());

        if (motor_tick_is_limit_hit(ops, id, ctx->speed_ref))
        {
            LOG_INFO("motor[%s]: limit reached", cfg->name);
            motor_record_done_event_locked(tick, id, SW_OK);
            continue;
        }

        if (motor_tick_handle_target_reached_locked(tick, id, cfg, ctx))
        {
            continue;
        }

        if (motor_tick_handle_move_time_locked(tick, id, cfg, ctx, elapsed_ms))
        {
            continue;
        }

        if (motor_tick_handle_timeout_locked(tick, id, cfg, elapsed_ms))
        {
            continue;
        }
    }

    return true;
}

static void motor_tick_notify_done(const motor_tick_ctx_t *tick)
{
    if (tick == NULL)
    {
        return;
    }

    for (int i = 0; i < tick->done_count; i++)
    {
        motor_notify_done(tick->done_ids[i], tick->done_results[i]);
    }
}

void *motor_tick_loop(void *arg)
{
    (void)arg;

    while (true)
    {
        motor_tick_ctx_t       tick;
        uint32_t               now_ms = time_util_get_ms();
        const hal_motor_ops_t *ops = hal_motor_get_ops();

        motor_tick_reset(&tick);

        pthread_mutex_lock(&s_mutex);
        if (!motor_tick_prepare_locked(&tick, ops, now_ms))
        {
            pthread_mutex_unlock(&s_mutex);
            usleep((unsigned long)THD_MOTOR_TICK_PERIOD_MS * 1000UL);
            continue;
        }
        pthread_mutex_unlock(&s_mutex);

        motor_tick_execute_encoder_jobs(&tick, ops);
        motor_monitor_collect_samples(tick.monitor_jobs, ops);

        pthread_mutex_lock(&s_mutex);
        if (!motor_tick_finalize_encoder_locked(&tick, ops) ||
            !s_initialized)
        {
            pthread_mutex_unlock(&s_mutex);
            usleep((unsigned long)THD_MOTOR_TICK_PERIOD_MS * 1000UL);
            continue;
        }

        motor_monitor_apply_faults_locked(tick.monitor_jobs, ops);
        if (!motor_tick_collect_done_locked(&tick, ops))
        {
            pthread_mutex_unlock(&s_mutex);
            usleep((unsigned long)THD_MOTOR_TICK_PERIOD_MS * 1000UL);
            continue;
        }
        pthread_mutex_unlock(&s_mutex);

        motor_tick_notify_done(&tick);
        usleep((unsigned long)THD_MOTOR_TICK_PERIOD_MS * 1000UL);
    }
}
