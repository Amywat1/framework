/**
 * @file    motor_encoder_internal.c
 * @brief   motor 模块内部编码器链路实现
 * @author  HUWANGWEI
 * @date    2026-04-14
 *
 * @note    本文件只处理编码器计数、清零和异常检测。
 *          调用方负责决定是否已持有 s_mutex，并按接口注释传入上下文。
 */

#include "domain/device/actuator/motor/motor_internal.h"

#include "common/time_util.h"
#include "common/log.h"

#include <unistd.h>

static bool motor_has_valid_encoder(const motor_cfg_t *cfg)
{
    return (cfg != NULL) && cfg->has_encoder;
}

static void motor_clear_encoder_error_locked(int id, const motor_cfg_t *cfg, motor_ctx_t *ctx)
{
    if ((cfg == NULL) || (ctx == NULL) || !ctx->encoder_err_reported)
    {
        return;
    }

    ctx->encoder_err_reported = false;
    (void)id;
}

static void motor_encoder_begin_counting_locked(int id,
                                                const motor_cfg_t *cfg,
                                                motor_ctx_t *ctx,
                                                uint32_t now_ms)
{
    if ((cfg == NULL) || (ctx == NULL))
    {
        return;
    }

    if (!ctx->encoder_counting)
    {
        ctx->encoder_counting       = true;
        ctx->encoder_no_change_cnt  = 0U;
        ctx->encoder_check_ms       = now_ms;
        ctx->encoder_check_snapshot = ctx->encoder_pos;
        motor_clear_encoder_error_locked(id, cfg, ctx);
    }
}

static void motor_encoder_end_counting_locked(int id,
                                              const motor_cfg_t *cfg,
                                              motor_ctx_t *ctx)
{
    if ((cfg == NULL) || (ctx == NULL))
    {
        return;
    }

    ctx->encoder_counting       = false;
    ctx->encoder_no_change_cnt  = 0U;
    ctx->encoder_check_snapshot = ctx->encoder_pos;
    motor_clear_encoder_error_locked(id, cfg, ctx);
}

static void encoder_apply_hw_counter_sample_locked(const motor_cfg_t *cfg,
                                                   motor_ctx_t *ctx,
                                                   const encoder_hw_job_t *job)
{
    uint32_t diff;

    if ((cfg == NULL) || (ctx == NULL) || (job == NULL) || (job->read_ret != SW_OK))
    {
        return;
    }

    if (!ctx->encoder_hw_last_valid)
    {
        ctx->encoder_hw_last       = job->read_value;
        ctx->encoder_hw_last_valid = true;
        return;
    }

    diff = job->read_value - ctx->encoder_hw_last;
    ctx->encoder_hw_last = job->read_value;

    if ((diff == 0U) || !ctx->encoder_counting)
    {
        return;
    }

    if (ctx->stored_dir > 0)
    {
        ctx->encoder_pos += (int32_t)diff;
    }
    else if (ctx->stored_dir < 0)
    {
        ctx->encoder_pos -= (int32_t)diff;
    }
}

static void encoder_apply_hw_clear_result_locked(int id,
                                                 const motor_cfg_t *cfg,
                                                 motor_ctx_t *ctx,
                                                 const encoder_hw_job_t *job)
{
    if ((cfg == NULL) || (ctx == NULL) || (job == NULL) || !job->need_clear)
    {
        return;
    }

    if (job->clear_ret != SW_OK)
    {
        ctx->clear_retry_pending = true;
        LOG_WARN("motor[%s]: clear_hw_pulse failed ret=%d", cfg->name, (int)job->clear_ret);
        return;
    }

    if (job->clear_baseline_valid)
    {
        ctx->encoder_hw_last       = job->clear_baseline;
        ctx->encoder_hw_last_valid = true;
    }
    else
    {
        ctx->encoder_hw_last       = 0U;
        ctx->encoder_hw_last_valid = false;
    }

    ctx->encoder_pos            = 0;
    ctx->clear_retry_pending    = false;
    ctx->encoder_check_snapshot = 0;
    ctx->encoder_check_ms       = time_util_get_ms();
    ctx->encoder_no_change_cnt  = 0U;
    motor_clear_encoder_error_locked(id, cfg, ctx);

    LOG_DEBUG("motor[%s]: encoder cleared", cfg->name);
}

static void encoder_handle_board_offline_locked(const motor_cfg_t *cfg,
                                                motor_ctx_t *ctx)
{
    if ((cfg == NULL) || (ctx == NULL) || !motor_needs_encoder_hw_io(cfg))
    {
        return;
    }

    ctx->encoder_hw_last       = 0U;
    ctx->encoder_hw_last_valid = false;
}

static void encoder_check_anomaly_locked(int id,
                                         const motor_cfg_t *cfg,
                                         motor_ctx_t *ctx,
                                         uint32_t now_ms)
{
    int64_t diff;
    int64_t abs_diff;

    if ((cfg == NULL) || (ctx == NULL) || (cfg->encoder_err_threshold == 0U))
    {
        return;
    }

    if ((cfg->encoder_err_check_ms == 0U) ||
        (time_elapsed_ms(ctx->encoder_check_ms, now_ms) < cfg->encoder_err_check_ms))
    {
        return;
    }

    ctx->encoder_check_ms = now_ms;

    if (!motor_is_move_state(ctx->state))
    {
        ctx->encoder_no_change_cnt  = 0U;
        ctx->encoder_check_snapshot = ctx->encoder_pos;
        return;
    }

    diff     = (int64_t)ctx->encoder_pos - (int64_t)ctx->encoder_check_snapshot;
    abs_diff = (diff >= 0) ? diff : -diff;

    if ((cfg->encoder_jump_threshold > 0U) &&
        (abs_diff > (int64_t)cfg->encoder_jump_threshold))
    {
        if (!ctx->encoder_err_reported)
        {
            ctx->encoder_err_reported = true;
            LOG_WARN("motor[%s]: encoder jump detected diff=%d",
                     cfg->name, (int)diff);
        }
        ctx->encoder_check_snapshot = ctx->encoder_pos;
        return;
    }

    if (abs_diff == 0)
    {
        if (ctx->encoder_no_change_cnt < 0xFFU)
        {
            ctx->encoder_no_change_cnt++;
        }

        if ((ctx->encoder_no_change_cnt >= cfg->encoder_err_threshold) &&
            !ctx->encoder_err_reported)
        {
            ctx->encoder_err_reported = true;
            LOG_WARN("motor[%s]: encoder no change cnt=%u",
                     cfg->name, (unsigned)ctx->encoder_no_change_cnt);
        }
    }
    else
    {
        ctx->encoder_no_change_cnt = 0U;
        motor_clear_encoder_error_locked(id, cfg, ctx);
    }

    ctx->encoder_check_snapshot = ctx->encoder_pos;
}

void motor_encoder_update_locked(int id,
                                 const hal_motor_ops_t *ops,
                                 const motor_cfg_t *cfg,
                                 motor_ctx_t *ctx,
                                 uint32_t now_ms)
{
    bool is_moving;

    (void)ops;

    if (!motor_has_valid_encoder(cfg))
    {
        return;
    }

    is_moving = motor_is_move_state(ctx->state);
    if (is_moving)
    {
        motor_encoder_begin_counting_locked(id, cfg, ctx, now_ms);
    }
    else if (ctx->encoder_counting &&
             (time_elapsed_ms(ctx->stop_timestamp_ms, now_ms) > 1000U))
    {
        motor_encoder_end_counting_locked(id, cfg, ctx);
    }
}

void motor_encoder_schedule_hw_job_locked(encoder_hw_job_t jobs[MOTOR_ID_MAX],
                                          const hal_motor_ops_t *ops,
                                          int id,
                                          const motor_cfg_t *cfg,
                                          const motor_ctx_t *ctx)
{
    (void)ops;

    if ((cfg == NULL) || (ctx == NULL) || !motor_needs_encoder_hw_io(cfg))
    {
        return;
    }

    if (ctx->clear_retry_pending)
    {
        jobs[id].need_clear = true;
        jobs[id].need_read  = false;
    }
    else if (ctx->encoder_counting || !ctx->encoder_hw_last_valid)
    {
        jobs[id].need_read = true;
    }
}

void motor_encoder_execute_hw_job(const hal_motor_ops_t *ops,
                                  const motor_cfg_t *cfg,
                                  int id,
                                  encoder_hw_job_t *job)
{
    (void)cfg;

    if (job == NULL)
    {
        return;
    }

    if (ops == NULL)
    {
        if (job->need_clear)
        {
            job->clear_ret = SW_ERR_NOT_INIT;
        }
        return;
    }

    if (job->need_clear)
    {
        uint8_t attempt;

        if ((ops->clear_hw_pulse == NULL) || (ops->read_hw_pulse == NULL))
        {
            job->clear_ret = SW_ERR_NOT_INIT;
            return;
        }

        for (attempt = 0U; attempt < MOTOR_ENCODER_CLEAR_RETRY_MAX; attempt++)
        {
            job->clear_ret = ops->clear_hw_pulse(id);
            if (job->clear_ret == SW_OK)
            {
                break;
            }
            if ((attempt + 1U) < MOTOR_ENCODER_CLEAR_RETRY_MAX)
            {
                usleep(MOTOR_ENCODER_CLEAR_RETRY_DELAY_US);
            }
        }

        if (job->clear_ret == SW_ERR_COMM)
        {
            job->board_offline = true;
        }

        if (job->clear_ret == SW_OK)
        {
            job->clear_baseline_valid =
                (ops->read_hw_pulse(id, &job->clear_baseline) == SW_OK);
        }
        return;
    }

    if (job->need_read)
    {
        if (ops->read_hw_pulse == NULL)
        {
            return;
        }
        job->read_ret = ops->read_hw_pulse(id, &job->read_value);
        if (job->read_ret == SW_ERR_COMM)
        {
            job->board_offline = true;
        }
    }
}

void motor_encoder_apply_clear_result_locked(int id,
                                             const motor_cfg_t *cfg,
                                             motor_ctx_t *ctx,
                                             const encoder_hw_job_t *job)
{
    encoder_apply_hw_clear_result_locked(id, cfg, ctx, job);
}

void motor_encoder_finalize_locked(int id,
                                   const hal_motor_ops_t *ops,
                                   const motor_cfg_t *cfg,
                                   motor_ctx_t *ctx,
                                   uint32_t now_ms,
                                   const encoder_hw_job_t *job)
{
    (void)ops;

    if ((cfg == NULL) || (ctx == NULL))
    {
        return;
    }

    if (motor_needs_encoder_hw_io(cfg) && (job != NULL))
    {
        if (job->board_offline)
        {
            encoder_handle_board_offline_locked(cfg, ctx);
        }

        if (job->need_clear)
        {
            encoder_apply_hw_clear_result_locked(id, cfg, ctx, job);
        }
        else if (job->need_read)
        {
            encoder_apply_hw_counter_sample_locked(cfg, ctx, job);
        }
    }

    encoder_check_anomaly_locked(id, cfg, ctx, now_ms);
}
