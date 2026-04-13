/**
 * @file    motor.c
 * @brief   通用电机管理层实现
 * @author  HUWANGWEI
 * @date    2026-04-13
 */

#include "domain/device/motor.h"
#include "config/machine/m8_motor_table.h"
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
} motor_ctx_t;

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
    return motor_set_state_locked(id, cfg, MOTOR_STATE_IDLE);
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
    uint32_t hw_value = 0U;

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

    if (cfg->encoder_use_hw_counter &&
        (ops != NULL) &&
        (ops->read_hw_pulse != NULL) &&
        !ctx->encoder_hw_last_valid)
    {
        if (ops->read_hw_pulse(id, &hw_value) == SW_OK)
        {
            ctx->encoder_hw_last       = hw_value;
            ctx->encoder_hw_last_valid = true;
        }
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

static void encoder_update_hw_counter_locked(int id,
                                             const hal_motor_ops_t *ops,
                                             const motor_cfg_t *cfg,
                                             motor_ctx_t *ctx)
{
    uint32_t hw_value = 0U;
    uint32_t diff;
    sw_err_t ret;

    if ((ops == NULL) || (ops->read_hw_pulse == NULL))
    {
        return;
    }

    ret = ops->read_hw_pulse(id, &hw_value);
    if (ret != SW_OK)
    {
        LOG_DEBUG("motor[%s]: hw pulse read failed ret=%d", cfg->name, (int)ret);
        return;
    }

    if (!ctx->encoder_hw_last_valid)
    {
        ctx->encoder_hw_last       = hw_value;
        ctx->encoder_hw_last_valid = true;
        return;
    }

    diff = hw_value - ctx->encoder_hw_last;
    ctx->encoder_hw_last = hw_value;

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

static sw_err_t encoder_do_clear_locked(int id,
                                        const hal_motor_ops_t *ops,
                                        const motor_cfg_t *cfg,
                                        motor_ctx_t *ctx)
{
    sw_err_t  ret      = SW_OK;
    uint32_t  hw_value = 0U;

    if ((cfg == NULL) || (ctx == NULL))
    {
        return SW_ERR_PARAM;
    }

    if (cfg->encoder_use_hw_counter)
    {
        if ((ops == NULL) || (ops->clear_hw_pulse == NULL) || (ops->read_hw_pulse == NULL))
        {
            return SW_ERR_NOT_INIT;
        }

        ret = ops->clear_hw_pulse(id);
        if (ret != SW_OK)
        {
            ctx->zero_clear_pending = true;
            LOG_WARN("motor[%s]: clear_hw_pulse failed ret=%d", cfg->name, (int)ret);
            return ret;
        }

        if (ops->read_hw_pulse(id, &hw_value) == SW_OK)
        {
            ctx->encoder_hw_last       = hw_value;
            ctx->encoder_hw_last_valid = true;
        }
        else
        {
            ctx->encoder_hw_last       = 0U;
            ctx->encoder_hw_last_valid = false;
        }

        ctx->encoder_pos = 0;
    }
    else if ((ops != NULL) && (ops->clear_pos != NULL))
    {
        ret = ops->clear_pos(id);
        if (ret != SW_OK)
        {
            LOG_WARN("motor[%s]: clear_pos failed ret=%d", cfg->name, (int)ret);
            return ret;
        }

        ctx->encoder_pos = 0;
    }
    else
    {
        return SW_ERR_NOT_INIT;
    }

    ctx->zero_clear_pending     = false;
    ctx->zero_confirm_cnt       = 0U;
    ctx->encoder_check_snapshot = 0;
    ctx->encoder_check_ms       = time_util_get_ms();
    ctx->encoder_no_change_cnt  = 0U;
    motor_clear_encoder_error_locked(id, cfg, ctx);

    LOG_DEBUG("motor[%s]: encoder cleared", cfg->name);
    return SW_OK;
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
            if (encoder_do_clear_locked(id, ops, cfg, ctx) == SW_OK)
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

    if (cfg->encoder_use_hw_counter)
    {
        encoder_update_hw_counter_locked(id, ops, cfg, ctx);
    }
    else if ((ops != NULL) && (ops->get_pos != NULL))
    {
        ctx->encoder_pos = ops->get_pos(id);
    }

    if (ctx->zero_clear_pending)
    {
        (void)encoder_do_clear_locked(id, ops, cfg, ctx);
    }

    encoder_check_zero_locked(id, ops, cfg, ctx);
    encoder_check_anomaly_locked(id, cfg, ctx, now_ms);
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

    ret = encoder_do_clear_locked(id, ops, cfg, &s_ctx[id]);
    pthread_mutex_unlock(&s_mutex);
    return ret;
}

void *motor_tick_loop(void *arg)
{
    (void)arg;

    while (true)
    {
        uint32_t done_params[MOTOR_ID_MAX];
        int      done_count = 0;
        uint32_t now_ms     = time_util_get_ms();

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
            const hal_motor_ops_t *ops = hal_motor_get_ops();
            motor_ctx_t           *ctx;
            uint32_t               elapsed_ms;
            int32_t                pos;
            bool                   hit_limit = false;

            if ((cfg == NULL) || (ops == NULL))
            {
                continue;
            }

            ctx = &s_ctx[id];
            motor_update_encoder_locked(id, ops, cfg, ctx, now_ms);

            if (!motor_is_move_state(ctx->state))
            {
                continue;
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
