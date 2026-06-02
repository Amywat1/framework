/**
 * @file    motor.c
 * @brief   通用电机管理层实现
 * @author  HUWANGWEI
 * @date    2026-04-13
 */

#include "domain/device/actuator/motor/motor_internal.h"

#include "config/machine/m8_motor_table.h"
#include "common/time_util.h"
#include "common/log.h"

#include <string.h>

pthread_mutex_t    s_mutex = PTHREAD_MUTEX_INITIALIZER;
static const motor_cfg_t *s_cfg_map[MOTOR_ID_MAX];
motor_ctx_t        s_ctx[MOTOR_ID_MAX];
bool               s_initialized = false;

/* =========================================================================
 * 基础工具函数（仅做查询和轻量判断）
 * ========================================================================= */

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

const motor_cfg_t *motor_get_cfg_locked(int id)
{
    if ((id < 0) || (id >= MOTOR_ID_MAX))
    {
        return NULL;
    }
    return s_cfg_map[id];
}

static const motor_cfg_t *motor_get_runtime_cfg_locked(int id, motor_ctx_t **p_ctx)
{
    const motor_cfg_t *cfg = motor_get_cfg_locked(id);

    if ((cfg == NULL) || !s_initialized)
    {
        return NULL;
    }

    if (p_ctx != NULL)
    {
        *p_ctx = &s_ctx[id];
    }

    return cfg;
}

static sw_err_t motor_require_runtime_cfg_locked(int id,
                                                 motor_ctx_t **p_ctx,
                                                 const motor_cfg_t **p_cfg)
{
    const motor_cfg_t *cfg = motor_get_runtime_cfg_locked(id, p_ctx);

    if (cfg == NULL)
    {
        return SW_ERR_NOT_INIT;
    }

    if (p_cfg != NULL)
    {
        *p_cfg = cfg;
    }

    return SW_OK;
}

static sw_err_t motor_require_action_cfg_locked(int id,
                                                motor_action_type_t action_type,
                                                bool require_encoder,
                                                const motor_cfg_t **p_cfg)
{
    const motor_cfg_t *cfg;
    sw_err_t           ret;

    ret = motor_require_runtime_cfg_locked(id, NULL, &cfg);
    if (ret != SW_OK)
    {
        return ret;
    }
    if (cfg->action_type != action_type)
    {
        return SW_ERR_PARAM;
    }
    if (require_encoder && !cfg->has_encoder)
    {
        return SW_ERR_PARAM;
    }

    if (p_cfg != NULL)
    {
        *p_cfg = cfg;
    }

    return SW_OK;
}

static sw_err_t motor_require_hw_encoder_cfg_locked(int id,
                                                    const hal_motor_ops_t *ops,
                                                    const motor_cfg_t **p_cfg)
{
    const motor_cfg_t *cfg;
    sw_err_t           ret;

    ret = motor_require_runtime_cfg_locked(id, NULL, &cfg);
    if (ret != SW_OK)
    {
        return ret;
    }
    if (!cfg->has_encoder)
    {
        return SW_ERR_PARAM;
    }
    if (!motor_needs_encoder_hw_io(cfg))
    {
        return SW_ERR_NOT_SUPPORT;
    }
    if (ops == NULL)
    {
        return SW_ERR_NOT_INIT;
    }

    if (p_cfg != NULL)
    {
        *p_cfg = cfg;
    }

    return SW_OK;
}

bool motor_is_move_state(motor_state_t state)
{
    return (state == MOTOR_STATE_MOVE) ||
           (state == MOTOR_STATE_MOVE_POS) ||
           (state == MOTOR_STATE_MOVE_TIME);
}

bool motor_is_running_state(motor_state_t state)
{
    return (state == MOTOR_STATE_HOLD) || motor_is_move_state(state);
}

motor_monitor_source_t motor_get_monitor_source(const motor_cfg_t *cfg)
{
    if ((cfg == NULL) || (cfg->drv_type != MOTOR_DRV_VFD))
    {
        return MOTOR_MON_SRC_NONE;
    }

    if (cfg->id == MOTOR_GANTRY)
    {
        return MOTOR_MON_SRC_VFD_GANTRY;
    }

    return MOTOR_MON_SRC_NONE;
}

uint32_t motor_calc_elapsed_ms_locked(const motor_ctx_t *ctx, uint32_t now_ms)
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

bool motor_needs_encoder_hw_io(const motor_cfg_t *cfg)
{
    return (cfg != NULL) && (cfg->encoder_backend == MOTOR_ENCODER_COUNTER);
}

bool motor_encoder_board_is_online(const hal_motor_ops_t *ops,
                                   int id,
                                   const motor_cfg_t *cfg)
{
    if (!motor_needs_encoder_hw_io(cfg))
    {
        return true;
    }
    if ((ops == NULL) || (ops->encoder_counter_online == NULL))
    {
        return true;
    }
    return ops->encoder_counter_online(id);
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

/* =========================================================================
 * 状态机核心（要求调用方已持有 s_mutex）
 * ========================================================================= */

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

    ctx->speed_ref          = speed_ref;
    ctx->stored_dir         = motor_dir_from_speed_ref(speed_ref);
    ctx->target_pos         = target_pos;
    ctx->move_time_ms       = move_time_ms;
    ctx->start_ms           = time_util_get_ms();
    ctx->stop_timestamp_ms  = 0U;
    ctx->pause_start_ms     = 0U;
    ctx->paused_total_ms    = 0U;
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

    ctx->speed_ref          = 0;
    ctx->applied_speed_ref  = 0;
    ctx->target_pos         = 0;
    ctx->move_time_ms       = 0U;
    ctx->start_ms           = 0U;
    ctx->stop_timestamp_ms  = time_util_get_ms();
    ctx->pause_start_ms     = 0U;
    ctx->paused_total_ms    = 0U;
    ctx->current_anomaly_ms = 0U;
    ctx->state_mismatch_ms  = 0U;
    ctx->load_current       = 0U;
    return motor_set_state_locked(id, cfg, MOTOR_STATE_IDLE);
}

void motor_enter_fault_locked(int id,
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

static sw_err_t motor_run_hold_or_move_command(int id,
                                               motor_action_type_t action_type,
                                               motor_state_t target_state,
                                               int speed_ref)
{
    sw_err_t           ret;

    pthread_mutex_lock(&s_mutex);
    ret = motor_require_action_cfg_locked(id, action_type, false, NULL);
    if (ret != SW_OK)
    {
        pthread_mutex_unlock(&s_mutex);
        return ret;
    }

    if (speed_ref == 0)
    {
        ret = motor_stop_locked(id);
    }
    else
    {
        ret = motor_start_locked(id, target_state, speed_ref, 0, 0U);
    }

    pthread_mutex_unlock(&s_mutex);
    return ret;
}

static sw_err_t motor_run_position_command(int id, int speed_ref, int32_t target_pos)
{
    sw_err_t           ret;

    pthread_mutex_lock(&s_mutex);
    ret = motor_require_action_cfg_locked(id, MOTOR_ACTION_MOVE, true, NULL);
    if (ret == SW_OK)
    {
        ret = motor_start_locked(id, MOTOR_STATE_MOVE_POS, speed_ref, target_pos, 0U);
    }
    pthread_mutex_unlock(&s_mutex);
    return ret;
}

static sw_err_t motor_run_timed_command(int id, int speed_ref, uint32_t duration_ms)
{
    sw_err_t           ret;

    pthread_mutex_lock(&s_mutex);
    ret = motor_require_action_cfg_locked(id, MOTOR_ACTION_MOVE, false, NULL);
    if (ret == SW_OK)
    {
        ret = motor_start_locked(id, MOTOR_STATE_MOVE_TIME, speed_ref, 0, duration_ms);
    }
    pthread_mutex_unlock(&s_mutex);
    return ret;
}

/* 返回 true 表示已写入完成事件参数，调用方应传入非空 event_param。 */
bool motor_finish_locked(int id, sw_err_t result, uint32_t *event_param)
{
    const motor_cfg_t *cfg = motor_get_cfg_locked(id);
    sw_err_t           stop_ret;

    stop_ret = motor_stop_locked(id);
    if ((result == SW_OK) && (stop_ret != SW_OK))
    {
        result = stop_ret;
    }

    if (motor_should_publish_done_event(cfg) && (event_param != NULL))
    {
        *event_param = MOTOR_DONE_PARAM_PACK(id, result);
        return true;
    }
    return false;
}

/* =========================================================================
 * 对外接口
 * ========================================================================= */

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
    memset(s_ctx, 0, sizeof(s_ctx));

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
    }

    s_initialized = true;
    pthread_mutex_unlock(&s_mutex);

    LOG_INFO("motor: init ok (%d motors)", M8_MOTOR_TABLE_SIZE);
    return SW_OK;
}

sw_err_t motor_hold(int id, int speed_ref)
{
    return motor_run_hold_or_move_command(id,
                                          MOTOR_ACTION_HOLD,
                                          MOTOR_STATE_HOLD,
                                          speed_ref);
}

sw_err_t motor_move(int id, int speed_ref)
{
    return motor_run_hold_or_move_command(id,
                                          MOTOR_ACTION_MOVE,
                                          MOTOR_STATE_MOVE,
                                          speed_ref);
}

sw_err_t motor_move_pos(int id, int speed_ref, int32_t target_pos)
{
    return motor_run_position_command(id, speed_ref, target_pos);
}

sw_err_t motor_move_time(int id, int speed_ref, uint32_t duration_ms)
{
    if (duration_ms == 0U)
    {
        return SW_ERR_PARAM;
    }

    return motor_run_timed_command(id, speed_ref, duration_ms);
}

sw_err_t motor_pause(int id)
{
    const motor_cfg_t      *cfg;
    const hal_motor_ops_t *ops = hal_motor_get_ops();
    motor_ctx_t           *ctx;
    sw_err_t               ret;

    pthread_mutex_lock(&s_mutex);
    ret = motor_require_runtime_cfg_locked(id, &ctx, &cfg);
    if (ret != SW_OK)
    {
        goto out;
    }
    if (!motor_is_move_state(ctx->state))
    {
        ret = SW_ERR_STATE;
        goto out;
    }

    ctx->paused_from    = ctx->state;
    ctx->pause_start_ms = time_util_get_ms();
    ret = motor_apply_output_locked(id, cfg, ops, 0);
    if (ret == SW_OK)
    {
        ctx->stop_timestamp_ms = ctx->pause_start_ms;
        (void)motor_set_state_locked(id, cfg, MOTOR_STATE_PAUSE);
    }
out:
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
    ret = motor_require_runtime_cfg_locked(id, &ctx, &cfg);
    if (ret != SW_OK)
    {
        goto out;
    }
    if (ctx->state != MOTOR_STATE_PAUSE)
    {
        ret = SW_ERR_STATE;
        goto out;
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
out:
    pthread_mutex_unlock(&s_mutex);
    return ret;
}

sw_err_t motor_stop(int id)
{
    sw_err_t ret;

    pthread_mutex_lock(&s_mutex);
    ret = motor_require_runtime_cfg_locked(id, NULL, NULL);
    if (ret != SW_OK)
    {
        goto out;
    }

    ret = motor_stop_locked(id);
out:
    pthread_mutex_unlock(&s_mutex);
    return ret;
}

sw_err_t motor_reset_fault(int id)
{
    const motor_cfg_t *cfg;
    motor_ctx_t       *ctx;
    sw_err_t           ret;

    pthread_mutex_lock(&s_mutex);
    ret = motor_require_runtime_cfg_locked(id, &ctx, &cfg);
    if (ret != SW_OK)
    {
        goto out;
    }

    if (ctx->state == MOTOR_STATE_FAULT)
    {
        memset(ctx, 0, sizeof(*ctx));
        ctx->state = MOTOR_STATE_FAULT;
        (void)motor_set_state_locked(id, cfg, MOTOR_STATE_IDLE);
        LOG_INFO("motor[%s]: fault reset", cfg->name);
    }
    ret = SW_OK;
out:
    pthread_mutex_unlock(&s_mutex);
    return ret;
}

motor_state_t motor_get_state(int id)
{
    motor_state_t      state = MOTOR_STATE_FAULT;
    const motor_cfg_t *cfg   = NULL;
    motor_ctx_t       *ctx   = NULL;

    pthread_mutex_lock(&s_mutex);
    if (motor_require_runtime_cfg_locked(id, &ctx, &cfg) == SW_OK)
    {
        state = ctx->state;
    }
    pthread_mutex_unlock(&s_mutex);
    return state;
}

int32_t motor_get_pos(int id)
{
    int32_t            pos = -1;
    const motor_cfg_t *cfg = NULL;
    motor_ctx_t       *ctx = NULL;

    pthread_mutex_lock(&s_mutex);
    if (motor_require_runtime_cfg_locked(id, &ctx, &cfg) == SW_OK &&
        cfg->has_encoder)
    {
        pos = ctx->encoder_pos;
    }
    pthread_mutex_unlock(&s_mutex);
    return pos;
}

sw_err_t motor_clear_encoder(int id)
{
    const motor_cfg_t      *cfg;
    const hal_motor_ops_t *ops = hal_motor_get_ops();
    motor_ctx_t           *ctx;
    sw_err_t               ret;
    encoder_hw_job_t       job;

    pthread_mutex_lock(&s_mutex);
    ret = motor_require_hw_encoder_cfg_locked(id, ops, &cfg);
    if (ret != SW_OK)
    {
        pthread_mutex_unlock(&s_mutex);
        return ret;
    }

    memset(&job, 0, sizeof(job));
    job.need_clear = true;
    pthread_mutex_unlock(&s_mutex);

    if (!motor_encoder_board_is_online(ops, id, cfg))
    {
        return SW_ERR_COMM;
    }

    motor_encoder_execute_hw_job(ops, cfg, id, &job);

    pthread_mutex_lock(&s_mutex);
    ret = motor_require_runtime_cfg_locked(id, &ctx, &cfg);
    if (ret != SW_OK)
    {
        pthread_mutex_unlock(&s_mutex);
        return ret;
    }
    motor_encoder_apply_clear_result_locked(id, cfg, ctx, &job);
    ret = job.clear_ret;
    pthread_mutex_unlock(&s_mutex);
    return ret;
}
