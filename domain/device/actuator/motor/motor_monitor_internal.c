/**
 * @file    motor_monitor_internal.c
 * @brief   motor 模块内部驱动器监测实现
 * @author  HUWANGWEI
 * @date    2026-04-14
 *
 * @note    本文件负责共享驱动器采样任务、故障判定和按 channel 传播故障态。
 */

#include "domain/device/actuator/motor/motor_internal.h"

#include "common/time_util.h"
#include "common/log.h"

typedef enum
{
    MOTOR_MON_FAULT_NONE = 0,
    MOTOR_MON_FAULT_CURRENT,
    MOTOR_MON_FAULT_STATUS,
} motor_mon_fault_t;

static bool motor_cfg_has_current_monitor(const motor_cfg_t *cfg)
{
    return (cfg != NULL) &&
           ((cfg->current_high_threshold > 0U) || (cfg->current_low_threshold > 0U));
}

static motor_mon_fault_t motor_apply_drv_sample_locked(const motor_cfg_t         *cfg,
                                                           motor_ctx_t               *ctx,
                                                           const motor_monitor_job_t *job,
                                                           uint32_t                   run_elapsed_ms,
                                                           uint32_t                   tick_ms)
{
    if ((cfg == NULL) || (ctx == NULL) || (job == NULL))
    {
        return MOTOR_MON_FAULT_NONE;
    }

    if (!motor_is_running_state(ctx->state))
    {
        motor_reset_vfd_runtime_stats_locked(ctx);
        return MOTOR_MON_FAULT_NONE;
    }

    if (job->current_valid && motor_cfg_has_current_monitor(cfg))
    {
        ctx->vfd_load_current = job->current;

        if (run_elapsed_ms < cfg->current_check_delay_ms)
        {
            ctx->current_fault_accum_ms = 0U;
        }
        else if ((cfg->current_high_threshold > 0U) &&
                 (job->current > cfg->current_high_threshold))
        {
            ctx->current_fault_accum_ms += tick_ms;
            if (ctx->current_fault_accum_ms >= cfg->current_confirm_ms)
            {
                LOG_WARN("motor[%s]: overcurrent %u > %u for %u ms",
                         cfg->name,
                         (unsigned)job->current,
                         (unsigned)cfg->current_high_threshold,
                         (unsigned)ctx->current_fault_accum_ms);
                return MOTOR_MON_FAULT_CURRENT;
            }
        }
        else if ((cfg->current_low_threshold > 0U) &&
                 (job->current < cfg->current_low_threshold))
        {
            ctx->current_fault_accum_ms += tick_ms;
            if (ctx->current_fault_accum_ms >= cfg->current_confirm_ms)
            {
                LOG_WARN("motor[%s]: undercurrent %u < %u for %u ms",
                         cfg->name,
                         (unsigned)job->current,
                         (unsigned)cfg->current_low_threshold,
                         (unsigned)ctx->current_fault_accum_ms);
                return MOTOR_MON_FAULT_CURRENT;
            }
        }
        else
        {
            ctx->current_fault_accum_ms = 0U;
        }
    }
    else
    {
        ctx->current_fault_accum_ms = 0U;
    }

    if (job->hw_running_valid)
    {
        if (run_elapsed_ms < cfg->current_check_delay_ms)
        {
            ctx->vfd_not_running_accum_ms = 0U;
        }
        else
        {
            if (!job->hw_is_running)
            {
                ctx->vfd_not_running_accum_ms += tick_ms;
                if (ctx->vfd_not_running_accum_ms >= MOTOR_MON_NOT_RUNNING_CONFIRM_MS)
                {
                    LOG_ERROR("motor[%s]: VFD hw not running for %u ms",
                              cfg->name, (unsigned)ctx->vfd_not_running_accum_ms);
                    return MOTOR_MON_FAULT_STATUS;
                }
            }
            else
            {
                ctx->vfd_not_running_accum_ms = 0U;
            }
        }
    }
    else
    {
        ctx->vfd_not_running_accum_ms = 0U;
    }

    return MOTOR_MON_FAULT_NONE;
}

static void motor_apply_ch_fault_locked(motor_mon_ch_t channel,
                                                 const hal_motor_ops_t  *ops)
{
    for (int id = 0; id < MOTOR_ID_MAX; id++)
    {
        const motor_cfg_t *cfg = motor_get_cfg_locked(id);
        motor_ctx_t       *ctx = &s_ctx[id];

        if ((cfg == NULL) || !motor_is_running_state(ctx->state))
        {
            continue;
        }
        if (motor_mon_ch_of(cfg) != channel)
        {
            continue;
        }

        motor_enter_fault_locked(id, cfg, ops);
    }
}

void motor_monitor_schedule_job_locked(motor_monitor_job_t jobs[MOTOR_MON_CH_MAX],
                                       const motor_cfg_t *cfg,
                                       const motor_ctx_t *ctx)
{
    motor_mon_ch_t channel;

    if ((cfg == NULL) || (ctx == NULL) || !motor_is_running_state(ctx->state))
    {
        return;
    }

    channel = motor_mon_ch_of(cfg);
    if ((channel <= MOTOR_MON_CH_NONE) || (channel >= MOTOR_MON_CH_MAX))
    {
        return;
    }

    if (!jobs[channel].used)
    {
        /* jobs 由 motor_tick_reset() 每拍 memset 清零，其余字段保持 0 即可 */
        jobs[channel].used           = true;
        jobs[channel].proxy_motor_id = cfg->id;
    }

    if (motor_cfg_has_current_monitor(cfg))
    {
        jobs[channel].need_current = true;
    }
}

void motor_monitor_collect_samples(motor_monitor_job_t jobs[MOTOR_MON_CH_MAX],
                                   const hal_motor_ops_t *ops)
{
    for (int channel = MOTOR_MON_CH_NONE + 1; channel < MOTOR_MON_CH_MAX; channel++)
    {
        motor_monitor_job_t *job = &jobs[channel];

        if (!job->used || (ops == NULL))
        {
            continue;
        }

        if (job->need_current && (ops->read_current != NULL))
        {
            job->current_valid =
                (ops->read_current(job->proxy_motor_id, &job->current) == SW_OK);
        }

        if (ops->read_running != NULL)
        {
            job->hw_running_valid =
                (ops->read_running(job->proxy_motor_id, &job->hw_is_running) == SW_OK);
        }
    }
}

void motor_monitor_apply_faults_locked(const motor_monitor_job_t jobs[MOTOR_MON_CH_MAX],
                                       const hal_motor_ops_t *ops)
{
    for (int channel = MOTOR_MON_CH_NONE + 1; channel < MOTOR_MON_CH_MAX; channel++)
    {
        const motor_monitor_job_t *job = &jobs[channel];
        bool                       should_fault = false;

        if (!job->used)
        {
            continue;
        }

        for (int id = 0; id < MOTOR_ID_MAX; id++)
        {
            const motor_cfg_t     *cfg = motor_get_cfg_locked(id);
            motor_ctx_t           *ctx = &s_ctx[id];
            motor_mon_fault_t  fault;
            uint32_t               elapsed_ms;

            if ((cfg == NULL) || !motor_is_running_state(ctx->state))
            {
                continue;
            }
            if (motor_mon_ch_of(cfg) != (motor_mon_ch_t)channel)
            {
                continue;
            }

            elapsed_ms = motor_calc_elapsed_ms_locked(ctx, time_util_get_ms());
            fault = motor_apply_drv_sample_locked(cfg,
                                                  ctx,
                                                  job,
                                                  elapsed_ms,
                                                  MOTOR_TICK_PERIOD_MS);
            if ((fault == MOTOR_MON_FAULT_CURRENT) || (fault == MOTOR_MON_FAULT_STATUS))
            {
                should_fault = true;
            }
        }

        if (should_fault)
        {
            motor_apply_ch_fault_locked((motor_mon_ch_t)channel, ops);
        }
    }
}
