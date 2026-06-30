/**
 * @file    motor_monitor_internal.c
 * @brief   motor 模块内部 VFD 监测实现
 * @author  HUWANGWEI
 * @date    2026-04-14
 *
 * @note    本文件负责共享 VFD 采样任务、故障判定和按 source 传播故障态。
 */

#include "domain/device/actuator/motor/motor_internal.h"

#include "common/time_util.h"
#include "common/log.h"

typedef enum
{
    MOTOR_MON_FAULT_NONE = 0,
    MOTOR_MON_FAULT_CURRENT,
    MOTOR_MON_FAULT_STATUS,
} motor_monitor_fault_t;

static bool motor_cfg_has_current_monitor(const motor_cfg_t *cfg)
{
    return (cfg != NULL) &&
           ((cfg->current_high_threshold > 0U) || (cfg->current_low_threshold > 0U));
}

static motor_monitor_fault_t motor_apply_vfd_sample_locked(const motor_cfg_t   *cfg,
                                                           motor_ctx_t         *ctx,
                                                           const motor_monitor_job_t *job,
                                                           uint32_t             run_elapsed_ms,
                                                           uint32_t             tick_ms)
{
    bool hw_running;

    if ((cfg == NULL) || (ctx == NULL) || (job == NULL))
    {
        return MOTOR_MON_FAULT_NONE;
    }

    if (!motor_is_running_state(ctx->state))
    {
        ctx->current_anomaly_ms = 0U;
        ctx->state_mismatch_ms  = 0U;
        ctx->load_current       = 0U;
        return MOTOR_MON_FAULT_NONE;
    }

    if (job->current_valid && motor_cfg_has_current_monitor(cfg))
    {
        ctx->load_current = job->current;

        if (run_elapsed_ms < cfg->current_check_delay_ms)
        {
            ctx->current_anomaly_ms = 0U;
        }
        else if ((cfg->current_high_threshold > 0U) &&
                 (job->current > cfg->current_high_threshold))
        {
            ctx->current_anomaly_ms += tick_ms;
            if (ctx->current_anomaly_ms >= cfg->current_confirm_ms)
            {
                LOG_WARN("motor[%s]: overcurrent %u > %u for %u ms",
                         cfg->name,
                         (unsigned)job->current,
                         (unsigned)cfg->current_high_threshold,
                         (unsigned)ctx->current_anomaly_ms);
                return MOTOR_MON_FAULT_CURRENT;
            }
        }
        else if ((cfg->current_low_threshold > 0U) &&
                 (job->current < cfg->current_low_threshold))
        {
            ctx->current_anomaly_ms += tick_ms;
            if (ctx->current_anomaly_ms >= cfg->current_confirm_ms)
            {
                LOG_WARN("motor[%s]: undercurrent %u < %u for %u ms",
                         cfg->name,
                         (unsigned)job->current,
                         (unsigned)cfg->current_low_threshold,
                         (unsigned)ctx->current_anomaly_ms);
                return MOTOR_MON_FAULT_CURRENT;
            }
        }
        else
        {
            ctx->current_anomaly_ms = 0U;
        }
    }
    else
    {
        ctx->current_anomaly_ms = 0U;
    }

    if (job->status_valid)
    {
        if (run_elapsed_ms < cfg->current_check_delay_ms)
        {
            ctx->state_mismatch_ms = 0U;
        }
        else
        {
            hw_running = ((job->status & MOTOR_VFD_STATUS_RUNNING_MASK) != 0U);
            if (!hw_running)
            {
                ctx->state_mismatch_ms += tick_ms;
                if (ctx->state_mismatch_ms >= MOTOR_VFD_STATE_CONFIRM_MS)
                {
                    LOG_ERROR("motor[%s]: VFD state mismatch, hw_status=0x%04X",
                              cfg->name, (unsigned)job->status);
                    return MOTOR_MON_FAULT_STATUS;
                }
            }
            else
            {
                ctx->state_mismatch_ms = 0U;
            }
        }
    }
    else
    {
        ctx->state_mismatch_ms = 0U;
    }

    return MOTOR_MON_FAULT_NONE;
}

static void motor_apply_source_fault_locked(motor_monitor_source_t  source,
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
        if (motor_get_monitor_source(cfg) != source)
        {
            continue;
        }

        motor_enter_fault_locked(id, cfg, ops);
    }
}

void motor_monitor_schedule_job_locked(motor_monitor_job_t jobs[MOTOR_MON_SRC_MAX],
                                       const motor_cfg_t *cfg,
                                       const motor_ctx_t *ctx)
{
    motor_monitor_source_t source;

    if ((cfg == NULL) || (ctx == NULL) || !motor_is_running_state(ctx->state))
    {
        return;
    }

    source = motor_get_monitor_source(cfg);
    if ((source <= MOTOR_MON_SRC_NONE) || (source >= MOTOR_MON_SRC_MAX))
    {
        return;
    }

    if (!jobs[source].used)
    {
        jobs[source].used            = true;
        jobs[source].sample_motor_id = cfg->id;
        jobs[source].need_current    = false;
        jobs[source].current_valid   = false;
        jobs[source].status_valid    = false;
        jobs[source].current         = 0U;
        jobs[source].status          = 0U;
    }

    if (motor_cfg_has_current_monitor(cfg))
    {
        jobs[source].need_current = true;
    }
}

void motor_monitor_collect_samples(motor_monitor_job_t jobs[MOTOR_MON_SRC_MAX],
                                   const hal_motor_ops_t *ops)
{
    for (int source = MOTOR_MON_SRC_NONE + 1; source < MOTOR_MON_SRC_MAX; source++)
    {
        motor_monitor_job_t *job = &jobs[source];

        if (!job->used || (ops == NULL))
        {
            continue;
        }

        if (job->need_current && (ops->read_current != NULL))
        {
            job->current_valid =
                (ops->read_current(job->sample_motor_id, &job->current) == SW_OK);
        }

        if (ops->read_status != NULL)
        {
            job->status_valid =
                (ops->read_status(job->sample_motor_id, &job->status) == SW_OK);
        }
    }
}

void motor_monitor_apply_faults_locked(const motor_monitor_job_t jobs[MOTOR_MON_SRC_MAX],
                                       const hal_motor_ops_t *ops)
{
    for (int source = MOTOR_MON_SRC_NONE + 1; source < MOTOR_MON_SRC_MAX; source++)
    {
        const motor_monitor_job_t *job          = &jobs[source];
        bool should_fault = false;

        if (!job->used)
        {
            continue;
        }

        for (int id = 0; id < MOTOR_ID_MAX; id++)
        {
            const motor_cfg_t    *cfg = motor_get_cfg_locked(id);
            motor_ctx_t          *ctx = &s_ctx[id];
            motor_monitor_fault_t fault;
            uint32_t              elapsed_ms;

            if ((cfg == NULL) || !motor_is_running_state(ctx->state))
            {
                continue;
            }
            if (motor_get_monitor_source(cfg) != (motor_monitor_source_t)source)
            {
                continue;
            }

            elapsed_ms = motor_calc_elapsed_ms_locked(ctx, time_util_get_ms());
            fault = motor_apply_vfd_sample_locked(cfg,
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
            motor_apply_source_fault_locked((motor_monitor_source_t)source, ops);
        }
    }
}
