/**
 * @file    motor.c
 * @brief   通用电机管理层实现
 * @author  HUWANGWEI
 * @date    2026-04-13
 */

#include "domain/device/motor.h"
#include "config/machine/m8_motor_table.h"
#include "domain/safety/alarm_core.h"
#include "ports/hal/hal_motor_port.h"
#include "config/threading/thread_config.h"
#include "core/event_bus/event_bus.h"
#include "common/event_types.h"
#include "common/time_util.h"
#include "common/log.h"
#include <pthread.h>
#include <stdbool.h>
#include <string.h>
#include <unistd.h>

typedef struct
{
    motor_state_t state;
    motor_state_t paused_from;
    int           speed_ref;
    int           applied_speed_ref;
    int           stored_dir;
    int32_t       target_pos;
    uint32_t      move_time_ms;
    uint32_t      start_ms;
    uint32_t      stop_timestamp_ms;
    uint32_t      pause_start_ms;
    uint32_t      paused_total_ms;

    /* ---- 码盘运行时状态 ---- */
    int32_t       encoder_pos;            /* 软件统一维护的位置值 */
    uint32_t      encoder_hw_last;        /* 上次读取的硬件脉冲计数器值 */
    bool          encoder_hw_last_valid;  /* 硬件脉冲基准是否已建立 */
    bool          encoder_counting;       /* 电机运动或停机惯性窗口内是否继续计数 */

    /* 零位校准 */
    uint8_t       zero_confirm_cnt;       /* 零位传感器连续触发计数 */
    bool          zero_clear_pending;     /* 清零失败后的重试标志 */

    /* 异常检测 */
    int32_t       encoder_check_snapshot; /* 上次检测位置快照 */
    uint32_t      encoder_check_ms;       /* 上次检测时间戳 */
    uint8_t       encoder_no_change_cnt;  /* 连续无变化计数 */
    bool          encoder_err_reported;   /* 是否已上报码盘异常 */

    /* VFD 运行监测 */
    uint16_t      load_current;           /* 最近一次读取到的负载电流 */
    uint32_t      current_anomaly_ms;     /* 电流异常累计时长 */
    uint32_t      state_mismatch_ms;      /* 运行状态不一致累计时长 */
} motor_ctx_t;

typedef enum
{
    MOTOR_MON_SRC_NONE = 0,
    MOTOR_MON_SRC_VFD_GANTRY,
    MOTOR_MON_SRC_VFD_BRUSH,
    MOTOR_MON_SRC_MAX
} motor_monitor_source_t;

typedef struct
{
    bool     used;
    int      sample_motor_id;
    bool     current_valid;
    bool     status_valid;
    uint16_t current;
    uint16_t status;
} motor_monitor_job_t;

typedef struct
{
    bool     need_read;
    bool     read_valid;
    uint32_t read_value;
    bool     need_clear;
    sw_err_t clear_ret;
    bool     clear_baseline_valid;
    uint32_t clear_baseline;
} encoder_hw_job_t;

typedef enum
{
    MOTOR_MON_FAULT_NONE = 0,
    MOTOR_MON_FAULT_CURRENT,
    MOTOR_MON_FAULT_STATUS,
} motor_monitor_fault_t;

static pthread_mutex_t    s_mutex = PTHREAD_MUTEX_INITIALIZER;
static const motor_cfg_t *s_cfg_map[MOTOR_ID_MAX];
static motor_ctx_t        s_ctx[MOTOR_ID_MAX];
static bool               s_initialized = false;

static const bool s_transition[MOTOR_STATE_MAX][MOTOR_STATE_MAX] = {
    [MOTOR_STATE_IDLE] = {
        [MOTOR_STATE_IDLE]      = true,
        [MOTOR_STATE_HOLD]      = true,
        [MOTOR_STATE_MOVE]      = true,
        [MOTOR_STATE_MOVE_POS]  = true,
        [MOTOR_STATE_MOVE_TIME] = true,
        [MOTOR_STATE_FAULT]     = true,
    },
    [MOTOR_STATE_HOLD] = {
        [MOTOR_STATE_IDLE]      = true,
        [MOTOR_STATE_HOLD]      = true,
        [MOTOR_STATE_FAULT]     = true,
    },
    [MOTOR_STATE_MOVE] = {
        [MOTOR_STATE_IDLE]      = true,
        [MOTOR_STATE_MOVE]      = true,
        [MOTOR_STATE_MOVE_POS]  = true,
        [MOTOR_STATE_MOVE_TIME] = true,
        [MOTOR_STATE_PAUSE]     = true,
        [MOTOR_STATE_FAULT]     = true,
    },
    [MOTOR_STATE_MOVE_POS] = {
        [MOTOR_STATE_IDLE]      = true,
        [MOTOR_STATE_MOVE]      = true,
        [MOTOR_STATE_MOVE_POS]  = true,
        [MOTOR_STATE_MOVE_TIME] = true,
        [MOTOR_STATE_PAUSE]     = true,
        [MOTOR_STATE_FAULT]     = true,
    },
    [MOTOR_STATE_MOVE_TIME] = {
        [MOTOR_STATE_IDLE]      = true,
        [MOTOR_STATE_MOVE]      = true,
        [MOTOR_STATE_MOVE_POS]  = true,
        [MOTOR_STATE_MOVE_TIME] = true,
        [MOTOR_STATE_PAUSE]     = true,
        [MOTOR_STATE_FAULT]     = true,
    },
    [MOTOR_STATE_PAUSE] = {
        [MOTOR_STATE_IDLE]      = true,
        [MOTOR_STATE_MOVE]      = true,
        [MOTOR_STATE_MOVE_POS]  = true,
        [MOTOR_STATE_MOVE_TIME] = true,
        [MOTOR_STATE_FAULT]     = true,
    },
    [MOTOR_STATE_FAULT] = {
        [MOTOR_STATE_IDLE]      = true,
        [MOTOR_STATE_FAULT]     = true,
    },
};

static const motor_cfg_t *motor_get_cfg_locked(int id)
{
    if ((id < 0) || (id >= MOTOR_ID_MAX))
    {
        return NULL;
    }
    return s_cfg_map[id];
}

static bool motor_is_move_state(motor_state_t state)
{
    return (state == MOTOR_STATE_MOVE) ||
           (state == MOTOR_STATE_MOVE_POS) ||
           (state == MOTOR_STATE_MOVE_TIME);
}

static bool motor_is_running_state(motor_state_t state)
{
    return (state == MOTOR_STATE_HOLD) || motor_is_move_state(state);
}

static motor_monitor_source_t motor_get_monitor_source(const motor_cfg_t *cfg)
{
    if ((cfg == NULL) || (cfg->drv_type != MOTOR_DRV_VFD))
    {
        return MOTOR_MON_SRC_NONE;
    }

    if (cfg->id == MOTOR_GANTRY)
    {
        return MOTOR_MON_SRC_VFD_GANTRY;
    }

    if ((cfg->id == MOTOR_BRUSH_TOP) || (cfg->id == MOTOR_BRUSH_SIDE))
    {
        return MOTOR_MON_SRC_VFD_BRUSH;
    }

    return MOTOR_MON_SRC_NONE;
}

static uint32_t motor_calc_elapsed_ms_locked(const motor_ctx_t *ctx, uint32_t now_ms)
{
    uint32_t elapsed_ms;

    if (ctx == NULL)
    {
        return 0U;
    }

    elapsed_ms = now_ms - ctx->start_ms;
    if (elapsed_ms >= ctx->paused_total_ms)
    {
        elapsed_ms -= ctx->paused_total_ms;
    }
    else
    {
        elapsed_ms = 0U;
    }

    return elapsed_ms;
}

static bool motor_needs_encoder_hw_io(const motor_cfg_t *cfg)
{
    return (cfg != NULL) && cfg->has_encoder && cfg->encoder_use_hw_counter;
}

static int motor_dir_from_speed_ref(int speed_ref)
{
    if (speed_ref > 0)
    {
        return 1;
    }
    if (speed_ref < 0)
    {
        return -1;
    }
    return 0;
}

static bool motor_has_valid_encoder(const motor_cfg_t *cfg)
{
    return (cfg != NULL) && cfg->has_encoder;
}

static bool motor_has_zero_sensor(const motor_cfg_t *cfg)
{
    return motor_has_valid_encoder(cfg) &&
           (io_di_raw(cfg->encoder_zero_io) != IO_HANDLE_NULL) &&
           (cfg->encoder_zero_confirm > 0U);
}

static void motor_publish_encoder_event(int id, int info)
{
    (void)event_publish(EVT_HW_ENCODER_ERR, MOTOR_EVENT_PARAM_PACK(id, info));
}

static sw_err_t motor_set_state_locked(int id, const motor_cfg_t *cfg, motor_state_t state)
{
    motor_ctx_t *ctx = &s_ctx[id];

    if ((state < 0) || (state >= MOTOR_STATE_MAX))
    {
        LOG_ERROR("motor[%s]: invalid target state=%d", cfg->name, (int)state);
        return SW_ERR_PARAM;
    }

    if (!s_transition[ctx->state][state])
    {
        LOG_ERROR("motor[%s]: illegal transition %d -> %d",
                  cfg->name, (int)ctx->state, (int)state);
        return SW_ERR_STATE;
    }

    if (ctx->state != state)
    {
        LOG_INFO("motor[%s]: %d -> %d", cfg->name, (int)ctx->state, (int)state);
        ctx->state = state;
    }

    return SW_OK;
}

static sw_err_t motor_apply_output_locked(int id,
                                          const motor_cfg_t *cfg,
                                          const hal_motor_ops_t *ops,
                                          int speed_ref)
{
    sw_err_t ret;

    if ((ops == NULL) || (ops->set_output == NULL))
    {
        LOG_ERROR("motor[%s]: hal_motor not ready", cfg->name);
        return SW_ERR_NOT_INIT;
    }

    ret = ops->set_output(id, speed_ref);
    if (ret != SW_OK)
    {
        LOG_ERROR("motor[%s]: set_output failed ret=%d", cfg->name, (int)ret);
        (void)motor_set_state_locked(id, cfg, MOTOR_STATE_FAULT);
        return ret;
    }

    s_ctx[id].applied_speed_ref = speed_ref;
    return SW_OK;
}

static sw_err_t motor_start_locked(int id,
                                   motor_state_t new_state,
                                   int speed_ref,
                                   int32_t target_pos,
                                   uint32_t move_time_ms)
{
    const motor_cfg_t      *cfg = motor_get_cfg_locked(id);
    const hal_motor_ops_t *ops = hal_motor_get_ops();
    motor_ctx_t           *ctx;
    sw_err_t               ret;

    if ((cfg == NULL) || !s_initialized)
    {
        return SW_ERR_NOT_INIT;
    }
    if (speed_ref == 0)
    {
        return SW_ERR_PARAM;
    }

    ctx = &s_ctx[id];
    if ((ctx->state == MOTOR_STATE_PAUSE) || (ctx->state == MOTOR_STATE_FAULT))
    {
        return SW_ERR_STATE;
    }
    if (!s_transition[ctx->state][new_state])
    {
        LOG_ERROR("motor[%s]: illegal transition %d -> %d",
                  cfg->name, (int)ctx->state, (int)new_state);
        return SW_ERR_STATE;
    }

    ret = motor_apply_output_locked(id, cfg, ops, speed_ref);
    if (ret != SW_OK)
    {
        return ret;
    }

    ctx->speed_ref       = speed_ref;
    ctx->stored_dir      = motor_dir_from_speed_ref(speed_ref);
    ctx->target_pos      = target_pos;
    ctx->move_time_ms    = move_time_ms;
    ctx->start_ms        = time_util_get_ms();
    ctx->stop_timestamp_ms = 0U;
    ctx->pause_start_ms  = 0U;
    ctx->paused_total_ms = 0U;
    ctx->current_anomaly_ms = 0U;
    ctx->state_mismatch_ms  = 0U;
    ctx->load_current       = 0U;
    return motor_set_state_locked(id, cfg, new_state);
}

static sw_err_t motor_stop_locked(int id)
{
    const motor_cfg_t      *cfg = motor_get_cfg_locked(id);
    const hal_motor_ops_t *ops = hal_motor_get_ops();
    motor_ctx_t           *ctx;
    sw_err_t               ret;

    if ((cfg == NULL) || !s_initialized)
    {
        return SW_ERR_NOT_INIT;
    }

    ctx = &s_ctx[id];
    if ((ops != NULL) && (ops->set_output != NULL))
    {
        ret = ops->set_output(id, 0);
        if (ret != SW_OK)
        {
            LOG_ERROR("motor[%s]: stop output failed ret=%d", cfg->name, (int)ret);
            (void)motor_set_state_locked(id, cfg, MOTOR_STATE_FAULT);
            return ret;
        }
    }

    ctx->speed_ref         = 0;
    ctx->applied_speed_ref = 0;
    ctx->target_pos        = 0;
    ctx->move_time_ms      = 0U;
    ctx->start_ms          = 0U;
    ctx->stop_timestamp_ms = time_util_get_ms();
    ctx->pause_start_ms    = 0U;
    ctx->paused_total_ms   = 0U;
    ctx->current_anomaly_ms = 0U;
    ctx->state_mismatch_ms  = 0U;
    ctx->load_current       = 0U;
    return motor_set_state_locked(id, cfg, MOTOR_STATE_IDLE);
}

static void motor_enter_fault_locked(int id,
                                     const motor_cfg_t *cfg,
                                     const hal_motor_ops_t *ops)
{
    motor_ctx_t *ctx = &s_ctx[id];

    if ((ops != NULL) && (ops->set_output != NULL))
    {
        (void)ops->set_output(id, 0);
    }

    ctx->speed_ref          = 0;
    ctx->applied_speed_ref  = 0;
    ctx->target_pos         = 0;
    ctx->move_time_ms       = 0U;
    ctx->pause_start_ms     = 0U;
    ctx->paused_total_ms    = 0U;
    ctx->stop_timestamp_ms  = time_util_get_ms();
    ctx->current_anomaly_ms = 0U;
    ctx->state_mismatch_ms  = 0U;

    (void)motor_set_state_locked(id, cfg, MOTOR_STATE_FAULT);
}

static bool motor_should_publish_done_event(const motor_cfg_t *cfg)
{
    return (cfg != NULL) && (cfg->action_type == MOTOR_ACTION_MOVE);
}

static bool motor_finish_locked(int id, sw_err_t result, uint32_t *event_param)
{
    const motor_cfg_t *cfg = motor_get_cfg_locked(id);
    sw_err_t           stop_ret;

    stop_ret = motor_stop_locked(id);
    if ((result == SW_OK) && (stop_ret != SW_OK))
    {
        result = stop_ret;
    }

    if (motor_should_publish_done_event(cfg))
    {
        *event_param = MOTOR_DONE_PARAM_PACK(id, result);
        return true;
    }
    return false;
}

static void motor_clear_encoder_error_locked(int id, const motor_cfg_t *cfg, motor_ctx_t *ctx)
{
    if ((cfg == NULL) || (ctx == NULL) || !ctx->encoder_err_reported)
    {
        return;
    }

    ctx->encoder_err_reported = false;
    motor_publish_encoder_event(id, 0);
}

static void motor_encoder_begin_counting_locked(int id,
                                                const motor_cfg_t *cfg,
                                                const hal_motor_ops_t *ops,
                                                motor_ctx_t *ctx,
                                                uint32_t now_ms)
{
    (void)id;
    (void)ops;

    if ((cfg == NULL) || (ctx == NULL))
    {
        return;
    }

    if (!ctx->encoder_counting)
    {
        ctx->encoder_counting       = true;
        ctx->zero_confirm_cnt       = 0U;
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
    ctx->zero_confirm_cnt       = 0U;
    ctx->encoder_no_change_cnt  = 0U;
    ctx->encoder_check_snapshot = ctx->encoder_pos;
    motor_clear_encoder_error_locked(id, cfg, ctx);
}

static void encoder_apply_hw_counter_sample_locked(int id,
                                                   const motor_cfg_t *cfg,
                                                   motor_ctx_t *ctx,
                                                   const encoder_hw_job_t *job)
{
    uint32_t diff;

    if ((cfg == NULL) || (ctx == NULL) || (job == NULL) || !job->read_valid)
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

    (void)id;
}

static sw_err_t encoder_clear_pos_locked(int id,
                                         const hal_motor_ops_t *ops,
                                         const motor_cfg_t *cfg,
                                         motor_ctx_t *ctx)
{
    sw_err_t ret;

    if ((cfg == NULL) || (ctx == NULL))
    {
        return SW_ERR_PARAM;
    }

    if ((ops == NULL) || (ops->clear_pos == NULL))
    {
        return SW_ERR_NOT_INIT;
    }

    ret = ops->clear_pos(id);
    if (ret != SW_OK)
    {
        LOG_WARN("motor[%s]: clear_pos failed ret=%d", cfg->name, (int)ret);
        return ret;
    }

    ctx->encoder_pos            = 0;
    ctx->zero_clear_pending     = false;
    ctx->zero_confirm_cnt       = 0U;
    ctx->encoder_check_snapshot = 0;
    ctx->encoder_check_ms       = time_util_get_ms();
    ctx->encoder_no_change_cnt  = 0U;
    motor_clear_encoder_error_locked(id, cfg, ctx);

    LOG_DEBUG("motor[%s]: encoder cleared", cfg->name);
    return SW_OK;
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
        ctx->zero_clear_pending = true;
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
    ctx->zero_clear_pending     = false;
    ctx->zero_confirm_cnt       = 0U;
    ctx->encoder_check_snapshot = 0;
    ctx->encoder_check_ms       = time_util_get_ms();
    ctx->encoder_no_change_cnt  = 0U;
    motor_clear_encoder_error_locked(id, cfg, ctx);

    LOG_DEBUG("motor[%s]: encoder cleared", cfg->name);
}

static void encoder_check_zero_locked(int id,
                                      const hal_motor_ops_t *ops,
                                      const motor_cfg_t *cfg,
                                      motor_ctx_t *ctx)
{
    bool at_zero;

    if ((cfg == NULL) || (ctx == NULL) || !motor_has_zero_sensor(cfg))
    {
        return;
    }

    if (!ctx->encoder_counting)
    {
        ctx->zero_confirm_cnt = 0U;
        return;
    }

    /*
     * 当前 M8 机型没有独立零位传感器，零位语义暂时等同于反向限位。
     * encoder_zero_io 仅作为配置层预留字段和启用条件使用。
     * 若后续新增“零位不等于反向限位”的电机，应在 hal_motor_ops_t 中增加
     * 专用 zero 读取接口，或由适配层按 encoder_zero_io 读取对应 DI 后返回。
     */
    at_zero = (ops != NULL) && (ops->at_rev_limit != NULL) && ops->at_rev_limit(id);
    if (at_zero)
    {
        if (ctx->zero_confirm_cnt < cfg->encoder_zero_confirm)
        {
            ctx->zero_confirm_cnt++;
        }
        if (ctx->zero_confirm_cnt == cfg->encoder_zero_confirm)
        {
            if (motor_needs_encoder_hw_io(cfg))
            {
                ctx->zero_clear_pending = true;
            }
            else if (encoder_clear_pos_locked(id, ops, cfg, ctx) == SW_OK)
            {
                ctx->zero_confirm_cnt = cfg->encoder_zero_confirm;
                LOG_INFO("motor[%s]: encoder zero calibrated", cfg->name);
            }
        }
    }
    else
    {
        ctx->zero_confirm_cnt = 0U;
    }
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
        ((now_ms - ctx->encoder_check_ms) < cfg->encoder_err_check_ms))
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
            motor_publish_encoder_event(id, ctx->stored_dir);
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
            motor_publish_encoder_event(id, ctx->stored_dir);
        }
    }
    else
    {
        ctx->encoder_no_change_cnt = 0U;
        motor_clear_encoder_error_locked(id, cfg, ctx);
    }

    ctx->encoder_check_snapshot = ctx->encoder_pos;
}

static void motor_update_encoder_locked(int id,
                                        const hal_motor_ops_t *ops,
                                        const motor_cfg_t *cfg,
                                        motor_ctx_t *ctx,
                                        uint32_t now_ms)
{
    bool is_moving;

    if (!motor_has_valid_encoder(cfg))
    {
        return;
    }

    is_moving = motor_is_move_state(ctx->state);
    if (is_moving)
    {
        motor_encoder_begin_counting_locked(id, cfg, ops, ctx, now_ms);
    }
    else if (ctx->encoder_counting &&
             ((now_ms - ctx->stop_timestamp_ms) > 1000U))
    {
        motor_encoder_end_counting_locked(id, cfg, ctx);
    }

    if (!cfg->encoder_use_hw_counter && (ops != NULL) && (ops->get_pos != NULL))
    {
        ctx->encoder_pos = ops->get_pos(id);
    }
}

#define MOTOR_VFD_STATUS_RUNNING_MASK  0x0001U  /* 士林状态字 bit0=运行中 */
#define MOTOR_VFD_STATE_CONFIRM_MS     3000U    /* 状态不一致持续 3s 判故障 */

static void encoder_schedule_hw_job_locked(encoder_hw_job_t      jobs[MOTOR_ID_MAX],
                                           int                   id,
                                           const motor_cfg_t    *cfg,
                                           const motor_ctx_t    *ctx)
{
    if ((cfg == NULL) || (ctx == NULL) || !motor_needs_encoder_hw_io(cfg))
    {
        return;
    }

    if (ctx->zero_clear_pending)
    {
        jobs[id].need_clear = true;
        jobs[id].need_read  = false;
    }
    else if (ctx->encoder_counting || !ctx->encoder_hw_last_valid)
    {
        jobs[id].need_read = true;
    }
}

static void encoder_execute_hw_job(const hal_motor_ops_t *ops,
                                   int                   id,
                                   encoder_hw_job_t     *job)
{
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
        if ((ops->clear_hw_pulse == NULL) || (ops->read_hw_pulse == NULL))
        {
            job->clear_ret = SW_ERR_NOT_INIT;
            return;
        }

        job->clear_ret = ops->clear_hw_pulse(id);
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
        job->read_valid = (ops->read_hw_pulse(id, &job->read_value) == SW_OK);
    }
}

static void motor_finalize_encoder_locked(int id,
                                          const hal_motor_ops_t *ops,
                                          const motor_cfg_t *cfg,
                                          motor_ctx_t *ctx,
                                          uint32_t now_ms,
                                          const encoder_hw_job_t *job)
{
    if ((cfg == NULL) || (ctx == NULL))
    {
        return;
    }

    if (motor_needs_encoder_hw_io(cfg) && (job != NULL))
    {
        if (job->need_clear)
        {
            encoder_apply_hw_clear_result_locked(id, cfg, ctx, job);
        }
        else if (job->need_read)
        {
            encoder_apply_hw_counter_sample_locked(id, cfg, ctx, job);
        }
    }
    else if (ctx->zero_clear_pending)
    {
        (void)encoder_clear_pos_locked(id, ops, cfg, ctx);
    }

    encoder_check_zero_locked(id, ops, cfg, ctx);
    encoder_check_anomaly_locked(id, cfg, ctx, now_ms);
}

static void motor_schedule_monitor_job_locked(motor_monitor_job_t        jobs[MOTOR_MON_SRC_MAX],
                                              const motor_cfg_t         *cfg,
                                              const motor_ctx_t         *ctx)
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
        jobs[source].current_valid   = false;
        jobs[source].status_valid    = false;
        jobs[source].current         = 0U;
        jobs[source].status          = 0U;
    }
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

    if (job->current_valid)
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

sw_err_t motor_init(void)
{
    const hal_motor_ops_t *ops = hal_motor_get_ops();

    if (ops == NULL)
    {
        LOG_ERROR("motor_init: hal_motor ops not registered");
        return SW_ERR_NOT_INIT;
    }

    pthread_mutex_lock(&s_mutex);
    memset(s_cfg_map, 0, sizeof(s_cfg_map));
    memset(s_ctx,     0, sizeof(s_ctx));

    for (int i = 0; i < M8_MOTOR_TABLE_SIZE; i++)
    {
        const motor_cfg_t *cfg = &m8_motor_table[i];

        if ((cfg->id < 0) || (cfg->id >= MOTOR_ID_MAX))
        {
            pthread_mutex_unlock(&s_mutex);
            LOG_ERROR("motor_init: invalid motor id=%d", cfg->id);
            return SW_ERR_PARAM;
        }
        s_cfg_map[cfg->id]   = cfg;
        s_ctx[cfg->id].state = MOTOR_STATE_IDLE;

        if (cfg->has_encoder && !cfg->encoder_use_hw_counter && (ops->get_pos != NULL))
        {
            s_ctx[cfg->id].encoder_pos            = ops->get_pos(cfg->id);
            s_ctx[cfg->id].encoder_check_snapshot = s_ctx[cfg->id].encoder_pos;
        }
    }

    s_initialized = true;
    pthread_mutex_unlock(&s_mutex);

    LOG_INFO("motor: init ok (%d motors)", M8_MOTOR_TABLE_SIZE);
    return SW_OK;
}

sw_err_t motor_hold(int id, int speed_ref)
{
    const motor_cfg_t *cfg;
    sw_err_t           ret;

    pthread_mutex_lock(&s_mutex);
    cfg = motor_get_cfg_locked(id);
    if ((cfg == NULL) || !s_initialized)
    {
        pthread_mutex_unlock(&s_mutex);
        return SW_ERR_NOT_INIT;
    }
    if (cfg->action_type != MOTOR_ACTION_HOLD)
    {
        pthread_mutex_unlock(&s_mutex);
        return SW_ERR_PARAM;
    }

    if (speed_ref == 0)
    {
        ret = motor_stop_locked(id);
        pthread_mutex_unlock(&s_mutex);
        return ret;
    }

    ret = motor_start_locked(id, MOTOR_STATE_HOLD, speed_ref, 0, 0U);
    pthread_mutex_unlock(&s_mutex);
    return ret;
}

sw_err_t motor_move(int id, int speed_ref)
{
    const motor_cfg_t *cfg;
    sw_err_t           ret;

    pthread_mutex_lock(&s_mutex);
    cfg = motor_get_cfg_locked(id);
    if ((cfg == NULL) || !s_initialized)
    {
        pthread_mutex_unlock(&s_mutex);
        return SW_ERR_NOT_INIT;
    }
    if (cfg->action_type != MOTOR_ACTION_MOVE)
    {
        pthread_mutex_unlock(&s_mutex);
        return SW_ERR_PARAM;
    }

    if (speed_ref == 0)
    {
        ret = motor_stop_locked(id);
        pthread_mutex_unlock(&s_mutex);
        return ret;
    }

    ret = motor_start_locked(id, MOTOR_STATE_MOVE, speed_ref, 0, 0U);
    pthread_mutex_unlock(&s_mutex);
    return ret;
}

sw_err_t motor_move_pos(int id, int speed_ref, int32_t target_pos)
{
    const motor_cfg_t *cfg;
    sw_err_t           ret;

    pthread_mutex_lock(&s_mutex);
    cfg = motor_get_cfg_locked(id);
    if ((cfg == NULL) || !s_initialized)
    {
        pthread_mutex_unlock(&s_mutex);
        return SW_ERR_NOT_INIT;
    }
    if ((cfg->action_type != MOTOR_ACTION_MOVE) || !cfg->has_encoder)
    {
        pthread_mutex_unlock(&s_mutex);
        return SW_ERR_PARAM;
    }

    ret = motor_start_locked(id, MOTOR_STATE_MOVE_POS, speed_ref, target_pos, 0U);
    pthread_mutex_unlock(&s_mutex);
    return ret;
}

sw_err_t motor_move_time(int id, int speed_ref, uint32_t duration_ms)
{
    const motor_cfg_t *cfg;
    sw_err_t           ret;

    if (duration_ms == 0U)
    {
        return SW_ERR_PARAM;
    }

    pthread_mutex_lock(&s_mutex);
    cfg = motor_get_cfg_locked(id);
    if ((cfg == NULL) || !s_initialized)
    {
        pthread_mutex_unlock(&s_mutex);
        return SW_ERR_NOT_INIT;
    }
    if (cfg->action_type != MOTOR_ACTION_MOVE)
    {
        pthread_mutex_unlock(&s_mutex);
        return SW_ERR_PARAM;
    }

    ret = motor_start_locked(id, MOTOR_STATE_MOVE_TIME, speed_ref, 0, duration_ms);
    pthread_mutex_unlock(&s_mutex);
    return ret;
}

sw_err_t motor_pause(int id)
{
    const motor_cfg_t      *cfg;
    const hal_motor_ops_t *ops = hal_motor_get_ops();
    motor_ctx_t           *ctx;
    sw_err_t               ret;

    pthread_mutex_lock(&s_mutex);
    cfg = motor_get_cfg_locked(id);
    if ((cfg == NULL) || !s_initialized)
    {
        pthread_mutex_unlock(&s_mutex);
        return SW_ERR_NOT_INIT;
    }

    ctx = &s_ctx[id];
    if (!motor_is_move_state(ctx->state))
    {
        pthread_mutex_unlock(&s_mutex);
        return SW_ERR_STATE;
    }

    ctx->paused_from    = ctx->state;
    ctx->pause_start_ms = time_util_get_ms();
    ret = motor_apply_output_locked(id, cfg, ops, 0);
    if (ret == SW_OK)
    {
        ctx->stop_timestamp_ms = ctx->pause_start_ms;
        (void)motor_set_state_locked(id, cfg, MOTOR_STATE_PAUSE);
    }
    pthread_mutex_unlock(&s_mutex);
    return ret;
}

sw_err_t motor_resume(int id)
{
    const motor_cfg_t      *cfg;
    const hal_motor_ops_t *ops = hal_motor_get_ops();
    motor_ctx_t           *ctx;
    sw_err_t               ret;
    uint32_t               now_ms;

    pthread_mutex_lock(&s_mutex);
    cfg = motor_get_cfg_locked(id);
    if ((cfg == NULL) || !s_initialized)
    {
        pthread_mutex_unlock(&s_mutex);
        return SW_ERR_NOT_INIT;
    }

    ctx = &s_ctx[id];
    if (ctx->state != MOTOR_STATE_PAUSE)
    {
        pthread_mutex_unlock(&s_mutex);
        return SW_ERR_STATE;
    }

    now_ms = time_util_get_ms();
    if (now_ms >= ctx->pause_start_ms)
    {
        ctx->paused_total_ms += (now_ms - ctx->pause_start_ms);
    }

    ret = motor_apply_output_locked(id, cfg, ops, ctx->speed_ref);
    if (ret == SW_OK)
    {
        (void)motor_set_state_locked(id, cfg, ctx->paused_from);
        ctx->pause_start_ms = 0U;
    }
    pthread_mutex_unlock(&s_mutex);
    return ret;
}

sw_err_t motor_stop(int id)
{
    sw_err_t ret;

    pthread_mutex_lock(&s_mutex);
    ret = motor_stop_locked(id);
    pthread_mutex_unlock(&s_mutex);
    return ret;
}

sw_err_t motor_reset_fault(int id)
{
    const motor_cfg_t *cfg;
    const hal_motor_ops_t *ops = hal_motor_get_ops();
    int32_t external_pos = 0;
    bool    has_external_pos = false;

    pthread_mutex_lock(&s_mutex);
    cfg = motor_get_cfg_locked(id);
    if ((cfg == NULL) || !s_initialized)
    {
        pthread_mutex_unlock(&s_mutex);
        return SW_ERR_NOT_INIT;
    }

    if (s_ctx[id].state == MOTOR_STATE_FAULT)
    {
        if ((cfg != NULL) && cfg->has_encoder && !cfg->encoder_use_hw_counter &&
            (ops != NULL) && (ops->get_pos != NULL))
        {
            external_pos     = ops->get_pos(id);
            has_external_pos = true;
        }

        memset(&s_ctx[id], 0, sizeof(s_ctx[id]));
        s_ctx[id].state = MOTOR_STATE_FAULT;
        if (has_external_pos)
        {
            s_ctx[id].encoder_pos            = external_pos;
            s_ctx[id].encoder_check_snapshot = external_pos;
        }
        (void)motor_set_state_locked(id, cfg, MOTOR_STATE_IDLE);
        LOG_INFO("motor[%s]: fault reset", cfg->name);
    }
    pthread_mutex_unlock(&s_mutex);
    return SW_OK;
}

motor_state_t motor_get_state(int id)
{
    motor_state_t state = MOTOR_STATE_FAULT;

    pthread_mutex_lock(&s_mutex);
    if (s_initialized && (id >= 0) && (id < MOTOR_ID_MAX) && (s_cfg_map[id] != NULL))
    {
        state = s_ctx[id].state;
    }
    pthread_mutex_unlock(&s_mutex);
    return state;
}

int32_t motor_get_pos(int id)
{
    int32_t pos = -1;

    pthread_mutex_lock(&s_mutex);
    if (s_initialized && (id >= 0) && (id < MOTOR_ID_MAX) && (s_cfg_map[id] != NULL) &&
        s_cfg_map[id]->has_encoder)
    {
        pos = s_ctx[id].encoder_pos;
    }
    pthread_mutex_unlock(&s_mutex);
    return pos;
}

sw_err_t motor_clear_encoder(int id)
{
    const motor_cfg_t      *cfg;
    const hal_motor_ops_t *ops = hal_motor_get_ops();
    sw_err_t               ret;
    encoder_hw_job_t       job;

    pthread_mutex_lock(&s_mutex);
    cfg = motor_get_cfg_locked(id);
    if ((cfg == NULL) || !s_initialized)
    {
        pthread_mutex_unlock(&s_mutex);
        return SW_ERR_NOT_INIT;
    }
    if (!cfg->has_encoder)
    {
        pthread_mutex_unlock(&s_mutex);
        return SW_ERR_PARAM;
    }
    if (ops == NULL)
    {
        pthread_mutex_unlock(&s_mutex);
        return SW_ERR_NOT_INIT;
    }

    if (motor_needs_encoder_hw_io(cfg))
    {
        memset(&job, 0, sizeof(job));
        job.need_clear = true;
        pthread_mutex_unlock(&s_mutex);

        encoder_execute_hw_job(ops, id, &job);

        pthread_mutex_lock(&s_mutex);
        cfg = motor_get_cfg_locked(id);
        if ((cfg == NULL) || !s_initialized)
        {
            pthread_mutex_unlock(&s_mutex);
            return SW_ERR_NOT_INIT;
        }
        encoder_apply_hw_clear_result_locked(id, cfg, &s_ctx[id], &job);
        ret = job.clear_ret;
    }
    else
    {
        ret = encoder_clear_pos_locked(id, ops, cfg, &s_ctx[id]);
    }
    pthread_mutex_unlock(&s_mutex);
    return ret;
}

void *motor_tick_loop(void *arg)
{
    (void)arg;

    while (true)
    {
        encoder_hw_job_t   encoder_jobs[MOTOR_ID_MAX];
        motor_monitor_job_t jobs[MOTOR_MON_SRC_MAX];
        uint32_t done_params[MOTOR_ID_MAX];
        int      done_count = 0;
        uint32_t now_ms     = time_util_get_ms();
        const hal_motor_ops_t *ops = hal_motor_get_ops();

        memset(encoder_jobs, 0, sizeof(encoder_jobs));
        memset(jobs, 0, sizeof(jobs));

        pthread_mutex_lock(&s_mutex);

        if (!s_initialized)
        {
            pthread_mutex_unlock(&s_mutex);
            usleep((unsigned long)THD_MOTOR_TICK_PERIOD_MS * 1000UL);
            continue;
        }

        for (int id = 0; id < MOTOR_ID_MAX; id++)
        {
            const motor_cfg_t      *cfg = motor_get_cfg_locked(id);
            motor_ctx_t           *ctx;

            if ((cfg == NULL) || (ops == NULL))
            {
                continue;
            }

            ctx = &s_ctx[id];
            motor_update_encoder_locked(id, ops, cfg, ctx, now_ms);
            encoder_schedule_hw_job_locked(encoder_jobs, id, cfg, ctx);
            motor_schedule_monitor_job_locked(jobs, cfg, ctx);
        }

        pthread_mutex_unlock(&s_mutex);

        for (int id = 0; id < MOTOR_ID_MAX; id++)
        {
            encoder_execute_hw_job(ops, id, &encoder_jobs[id]);
        }

        for (int source = MOTOR_MON_SRC_NONE + 1; source < MOTOR_MON_SRC_MAX; source++)
        {
            motor_monitor_job_t *job = &jobs[source];

            if (!job->used || (ops == NULL))
            {
                continue;
            }

            if (ops->read_current != NULL)
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

        pthread_mutex_lock(&s_mutex);
        if (!s_initialized)
        {
            pthread_mutex_unlock(&s_mutex);
            usleep((unsigned long)THD_MOTOR_TICK_PERIOD_MS * 1000UL);
            continue;
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
            motor_finalize_encoder_locked(id,
                                          ops,
                                          cfg,
                                          ctx,
                                          time_util_get_ms(),
                                          &encoder_jobs[id]);
        }

        for (int source = MOTOR_MON_SRC_NONE + 1; source < MOTOR_MON_SRC_MAX; source++)
        {
            const motor_monitor_job_t *job = &jobs[source];
            bool                       should_fault = false;
            uint16_t                   alarm_code = 0U;

            if (!job->used)
            {
                continue;
            }

            for (int id = 0; id < MOTOR_ID_MAX; id++)
            {
                const motor_cfg_t        *cfg = motor_get_cfg_locked(id);
                motor_ctx_t              *ctx = &s_ctx[id];
                motor_monitor_fault_t     fault;
                uint32_t                  elapsed_ms;

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
                                                      THD_MOTOR_TICK_PERIOD_MS);
                if (fault == MOTOR_MON_FAULT_CURRENT)
                {
                    should_fault = true;
                    if (cfg->alarm_code_current != 0U)
                    {
                        alarm_code = cfg->alarm_code_current;
                    }
                }
                else if (fault == MOTOR_MON_FAULT_STATUS)
                {
                    should_fault = true;
                    if (cfg->alarm_code_fault != 0U)
                    {
                        alarm_code = cfg->alarm_code_fault;
                    }
                }
            }

            if (should_fault)
            {
                if (alarm_code != 0U)
                {
                    alarm_core_set_state(alarm_code, true, false);
                }
                motor_apply_source_fault_locked((motor_monitor_source_t)source, ops);
            }
        }

        for (int id = 0; id < MOTOR_ID_MAX; id++)
        {
            const motor_cfg_t      *cfg = motor_get_cfg_locked(id);
            motor_ctx_t           *ctx;
            uint32_t               elapsed_ms;
            int32_t                pos;
            bool                   hit_limit = false;

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

            if (ctx->speed_ref > 0)
            {
                hit_limit = (ops->at_fwd_limit != NULL) && ops->at_fwd_limit(id);
            }
            else if (ctx->speed_ref < 0)
            {
                hit_limit = (ops->at_rev_limit != NULL) && ops->at_rev_limit(id);
            }

            if (hit_limit)
            {
                LOG_INFO("motor[%s]: limit reached", cfg->name);
                if (motor_finish_locked(id, SW_OK, &done_params[done_count]))
                {
                    done_count++;
                }
                continue;
            }

            if ((cfg->limit_mode == MOTOR_LIMIT_PULSE_MAX) ||
                (cfg->limit_mode == MOTOR_LIMIT_PULSE_MIN_MAX))
            {
                if (cfg->has_encoder)
                {
                    pos = ctx->encoder_pos;
                    if ((cfg->limit_mode == MOTOR_LIMIT_PULSE_MIN_MAX) &&
                        (pos <= (int32_t)cfg->limit_pos_min))
                    {
                        LOG_WARN("motor[%s]: pos min limit reached", cfg->name);
                        if (motor_finish_locked(id, SW_OK, &done_params[done_count]))
                        {
                            done_count++;
                        }
                        continue;
                    }
                    if (pos >= (int32_t)cfg->limit_pos_max)
                    {
                        LOG_WARN("motor[%s]: pos max limit reached", cfg->name);
                        if (motor_finish_locked(id, SW_OK, &done_params[done_count]))
                        {
                            done_count++;
                        }
                        continue;
                    }
                }
            }

            if ((ctx->state == MOTOR_STATE_MOVE_POS) && cfg->has_encoder)
            {
                pos = ctx->encoder_pos;
                bool reached = ((ctx->speed_ref > 0) && (pos >= ctx->target_pos)) ||
                               ((ctx->speed_ref < 0) && (pos <= ctx->target_pos));
                if (reached)
                {
                    LOG_INFO("motor[%s]: position reached target=%d actual=%d",
                             cfg->name, (int)ctx->target_pos, (int)pos);
                    if (motor_finish_locked(id, SW_OK, &done_params[done_count]))
                    {
                        done_count++;
                    }
                    continue;
                }
            }

            if ((ctx->state == MOTOR_STATE_MOVE_TIME) &&
                (elapsed_ms >= ctx->move_time_ms))
            {
                LOG_INFO("motor[%s]: move_time reached %u ms",
                         cfg->name, (unsigned)ctx->move_time_ms);
                if (motor_finish_locked(id, SW_OK, &done_params[done_count]))
                {
                    done_count++;
                }
                continue;
            }

            if ((cfg->timeout_ms != MOTOR_TIMEOUT_FOREVER) &&
                (elapsed_ms >= cfg->timeout_ms))
            {
                LOG_ERROR("motor[%s]: timeout after %u ms",
                          cfg->name, (unsigned)cfg->timeout_ms);
                if (motor_finish_locked(id, SW_ERR_TIMEOUT, &done_params[done_count]))
                {
                    done_count++;
                }
                continue;
            }
        }

        pthread_mutex_unlock(&s_mutex);

        for (int i = 0; i < done_count; i++)
        {
            (void)event_publish(EVT_COMP_MOTOR_DONE, done_params[i]);
        }

        usleep((unsigned long)THD_MOTOR_TICK_PERIOD_MS * 1000UL);
    }
}
