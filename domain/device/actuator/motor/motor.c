/**
 * @file    motor.c
 * @brief   通用电机管理层实现
 * @author  HUWANGWEI
 * @date    2026-04-13
 */

#include "domain/device/actuator/motor/motor_internal.h"

#include "machines/m8/config/m8_motor_table.h"
#include "common/time_util.h"
#include "common/log.h"

#include <string.h>

pthread_mutex_t    s_mutex = PTHREAD_MUTEX_INITIALIZER;
static const motor_cfg_t *s_cfg_map[MOTOR_ID_MAX];
motor_ctx_t        s_ctx[MOTOR_ID_MAX];
bool               s_initialized = false;

static motor_done_cb_t s_done_cbs[MOTOR_ID_MAX];
static void           *s_done_cb_ctxs[MOTOR_ID_MAX];

motor_pre_start_fn s_pre_start_fns[MOTOR_ID_MAX];
void              *s_pre_start_ctxs[MOTOR_ID_MAX];

/* =========================================================================
 * 基础工具函数（仅做查询和轻量判断）
 * ========================================================================= */

static const bool s_transition[MOTOR_STATE_MAX][MOTOR_STATE_MAX] = {
    [MOTOR_STATE_IDLE] = {
        [MOTOR_STATE_IDLE]      = true,
        [MOTOR_STATE_PENDING]   = true,
        [MOTOR_STATE_HOLD]      = true,
        [MOTOR_STATE_MOVE]      = true,
        [MOTOR_STATE_FAULT]     = true,
    },
    [MOTOR_STATE_PENDING] = {
        [MOTOR_STATE_IDLE]      = true,
        [MOTOR_STATE_PENDING]   = true,
        [MOTOR_STATE_HOLD]      = true,
        [MOTOR_STATE_MOVE]      = true,
        [MOTOR_STATE_FAULT]     = true,
    },
    [MOTOR_STATE_HOLD] = {
        [MOTOR_STATE_IDLE]      = true,
        [MOTOR_STATE_PENDING]   = true,
        [MOTOR_STATE_HOLD]      = true,
        [MOTOR_STATE_FAULT]     = true,
    },
    [MOTOR_STATE_MOVE] = {
        [MOTOR_STATE_IDLE]      = true,
        [MOTOR_STATE_MOVE]      = true,
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
    return (state == MOTOR_STATE_MOVE);
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
    if (cfg->id == MOTOR_BRUSH)
    {
        return MOTOR_MON_SRC_VFD_BRUSH;
    }

    return MOTOR_MON_SRC_NONE;
}

uint32_t motor_calc_elapsed_ms_locked(const motor_ctx_t *ctx, uint32_t now_ms)
{
    if ((ctx == NULL) || (ctx->start_ms == 0U))
    {
        return 0U;
    }

    return time_elapsed_ms(ctx->start_ms, now_ms);
}

bool motor_needs_encoder_hw_io(const motor_cfg_t *cfg)
{
    return (cfg != NULL) && (cfg->encoder_backend == MOTOR_ENCODER_COUNTER);
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

static sw_err_t motor_apply_gear_locked(int id,
                                         const motor_cfg_t *cfg,
                                         const hal_motor_ops_t *ops,
                                         int8_t gear)
{
    sw_err_t ret;

    if ((ops == NULL) || (ops->set_gear == NULL))
    {
        LOG_ERROR("motor[%s]: set_gear not supported", cfg->name);
        return SW_ERR_NOT_SUPPORT;
    }

    ret = ops->set_gear(id, gear);
    if (ret != SW_OK)
    {
        LOG_ERROR("motor[%s]: set_gear failed ret=%d", cfg->name, (int)ret);
        (void)motor_set_state_locked(id, cfg, MOTOR_STATE_FAULT);
        return ret;
    }

    s_ctx[id].applied_speed_ref = (int)gear; /* 借用字段记录当前挡位，便于调试 */
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

static void motor_reset_run_monitor_locked(motor_ctx_t *ctx)
{
    ctx->current_anomaly_ms = 0U;
    ctx->state_mismatch_ms  = 0U;
    ctx->load_current       = 0U;
}

static sw_err_t motor_start_locked(int id, motor_state_t new_state, int speed_ref)
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
    if (ctx->state == MOTOR_STATE_FAULT)
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

    ctx->speed_ref         = speed_ref;
    ctx->stored_dir        = motor_dir_from_speed_ref(speed_ref);
    ctx->start_ms          = time_util_get_ms();
    ctx->stop_timestamp_ms = 0U;
    motor_reset_run_monitor_locked(ctx);
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
    ctx->start_ms           = 0U;
    ctx->stop_timestamp_ms  = time_util_get_ms();
    motor_reset_run_monitor_locked(ctx);
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

    ctx->speed_ref         = 0;
    ctx->applied_speed_ref = 0;
    ctx->stop_timestamp_ms = time_util_get_ms();
    motor_reset_run_monitor_locked(ctx);

    (void)motor_set_state_locked(id, cfg, MOTOR_STATE_FAULT);
}

static bool motor_should_publish_done_event(const motor_cfg_t *cfg)
{
    return (cfg != NULL) && (cfg->action_type == MOTOR_ACTION_MOVE);
}

/* 若该电机配置了 post_stop_delay 或注册了 pre_start 回调，则进入 PENDING 状态，
 * 由 motor_tick 在延迟满足后调 pre_start 回调并真正启动；否则直接启动。
 *
 * 状态迁移路径：
 *   无需等待              → motor_start_locked()（直接 IDLE/HOLD → HOLD/MOVE）
 *   RUNNING + 需等待      → 清零 VFD 输出，记录 stop_timestamp_ms，→ PENDING
 *   已在 PENDING          → 仅更新目标参数（保留原计时，不重置延迟）
 *   IDLE   + 需等待/回调  → → PENDING（stop_timestamp_ms==0 时 tick 下拍立即触发）
 */
static sw_err_t motor_start_or_pend_locked(int id, motor_state_t target, int speed_ref)
{
    const motor_cfg_t     *cfg = motor_get_cfg_locked(id);
    const hal_motor_ops_t *ops = hal_motor_get_ops();
    motor_ctx_t           *ctx;
    uint32_t               now_ms;

    if ((cfg == NULL) || !s_initialized)
    {
        return SW_ERR_NOT_INIT;
    }
    ctx = &s_ctx[id];

    if ((cfg->post_stop_delay_ms == 0U) && (s_pre_start_fns[id] == NULL))
    {
        return motor_start_locked(id, target, speed_ref);
    }

    now_ms = time_util_get_ms();

    if (motor_is_running_state(ctx->state))
    {
        /* 清零 VFD 输出；状态仍为 HOLD/MOVE，下方统一迁移至 PENDING */
        (void)motor_apply_output_locked(id, cfg, ops, 0);
        ctx->stop_timestamp_ms = now_ms;
    }

    if (ctx->state == MOTOR_STATE_PENDING)
    {
        /* 已排队等待：更新目标参数，不重置延迟计时 */
        ctx->pending_speed_ref    = speed_ref;
        ctx->pending_target_state = target;
        return SW_OK;
    }

    ctx->pending_speed_ref    = speed_ref;
    ctx->pending_target_state = target;
    return motor_set_state_locked(id, cfg, MOTOR_STATE_PENDING);
}

/* 挡位模式版本：逻辑与 motor_start_or_pend_locked 相同，区别在于
 * 进入 PENDING 后记录 pending_is_gear=true + pending_gear_ref，
 * 由 motor_apply_pending_start_locked 走 set_gear 路径。 */
static sw_err_t motor_gear_pend_locked(int id, motor_state_t target, int8_t gear)
{
    const motor_cfg_t     *cfg = motor_get_cfg_locked(id);
    const hal_motor_ops_t *ops = hal_motor_get_ops();
    motor_ctx_t           *ctx;
    uint32_t               now_ms;

    if ((cfg == NULL) || !s_initialized)
    {
        return SW_ERR_NOT_INIT;
    }
    ctx = &s_ctx[id];

    /* 无需延迟且无 pre_start 回调：直接输出挡位 */
    if ((cfg->post_stop_delay_ms == 0U) && (s_pre_start_fns[id] == NULL))
    {
        sw_err_t ret = motor_apply_gear_locked(id, cfg, ops, gear);
        if (ret != SW_OK) { return ret; }
        ctx->speed_ref         = (int)gear;
        ctx->stored_dir        = 1;
        ctx->start_ms          = time_util_get_ms();
        ctx->stop_timestamp_ms = 0U;
        motor_reset_run_monitor_locked(ctx);
        return motor_set_state_locked(id, cfg, target);
    }

    now_ms = time_util_get_ms();

    if (motor_is_running_state(ctx->state))
    {
        (void)motor_apply_output_locked(id, cfg, ops, 0);
        ctx->stop_timestamp_ms = now_ms;
    }

    if (ctx->state == MOTOR_STATE_PENDING)
    {
        ctx->pending_is_gear      = true;
        ctx->pending_gear_ref     = gear;
        ctx->pending_target_state = target;
        return SW_OK;
    }

    ctx->pending_is_gear      = true;
    ctx->pending_gear_ref     = gear;
    ctx->pending_speed_ref    = 0;
    ctx->pending_target_state = target;
    return motor_set_state_locked(id, cfg, MOTOR_STATE_PENDING);
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
        ret = motor_start_or_pend_locked(id, target_state, speed_ref);
    }

    pthread_mutex_unlock(&s_mutex);
    return ret;
}

/* 返回 true 表示需要向上层发出完成通知（由 motor_tick 在锁外调用回调）。 */
bool motor_finish_locked(int id, sw_err_t result)
{
    const motor_cfg_t *cfg = motor_get_cfg_locked(id);
    sw_err_t           stop_ret;

    stop_ret = motor_stop_locked(id);
    if ((result == SW_OK) && (stop_ret != SW_OK))
    {
        result = stop_ret;
    }

    return motor_should_publish_done_event(cfg);
}

/* 在 motor_tick 释放锁后调用，触发已注册的完成回调。 */
void motor_notify_done(int id, sw_err_t result)
{
    motor_done_cb_t cb;
    void           *ctx;

    if ((id < 0) || (id >= MOTOR_ID_MAX))
    {
        return;
    }

    cb  = s_done_cbs[id];
    ctx = s_done_cb_ctxs[id];

    if (cb != NULL)
    {
        cb(id, result, ctx);
    }
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

sw_err_t motor_fault_reset(int id)
{
    const hal_motor_ops_t *ops = hal_motor_get_ops();

    if ((ops == NULL) || (ops->fault_reset == NULL))
    {
        return SW_ERR_NOT_SUPPORT;
    }
    return ops->fault_reset(id);
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

bool motor_at_fwd_limit(int id)
{
    const hal_motor_ops_t *ops = hal_motor_get_ops();

    if ((ops == NULL) || (ops->at_fwd_limit == NULL))
    {
        return false;
    }
    return ops->at_fwd_limit(id);
}

bool motor_at_rev_limit(int id)
{
    const hal_motor_ops_t *ops = hal_motor_get_ops();

    if ((ops == NULL) || (ops->at_rev_limit == NULL))
    {
        return false;
    }
    return ops->at_rev_limit(id);
}

sw_err_t motor_set_done_cb(int motor_id, motor_done_cb_t cb, void *ctx)
{
    if ((motor_id < 0) || (motor_id >= MOTOR_ID_MAX))
    {
        return SW_ERR_PARAM;
    }

    s_done_cbs[motor_id]     = cb;
    s_done_cb_ctxs[motor_id] = ctx;
    return SW_OK;
}

sw_err_t motor_set_pre_start_cb(int motor_id, motor_pre_start_fn cb, void *ctx)
{
    if ((motor_id < 0) || (motor_id >= MOTOR_ID_MAX))
    {
        return SW_ERR_PARAM;
    }

    s_pre_start_fns[motor_id]  = cb;
    s_pre_start_ctxs[motor_id] = ctx;
    return SW_OK;
}

sw_err_t motor_apply_pending_start_locked(int id, int speed_ref,
                                           motor_state_t target,
                                           uint32_t now_ms)
{
    const motor_cfg_t     *cfg = motor_get_cfg_locked(id);
    const hal_motor_ops_t *ops = hal_motor_get_ops();
    motor_ctx_t           *ctx;
    sw_err_t               ret;

    if ((cfg == NULL) || !s_initialized)
    {
        return SW_ERR_NOT_INIT;
    }
    ctx = &s_ctx[id];

    if (ctx->state != MOTOR_STATE_PENDING)
    {
        return SW_ERR_STATE;
    }

    if (ctx->pending_is_gear)
    {
        ret = motor_apply_gear_locked(id, cfg, ops, ctx->pending_gear_ref);
    }
    else
    {
        ret = motor_apply_output_locked(id, cfg, ops, speed_ref);
    }
    if (ret != SW_OK)
    {
        return ret;
    }

    ctx->speed_ref          = ctx->pending_is_gear ? (int)ctx->pending_gear_ref : speed_ref;
    ctx->stored_dir         = (ctx->speed_ref > 0) ? 1 : ((ctx->speed_ref < 0) ? -1 : 0);
    ctx->start_ms           = now_ms;
    ctx->stop_timestamp_ms  = 0U;
    motor_reset_run_monitor_locked(ctx);
    ctx->pending_is_gear    = false;
    ctx->pending_gear_ref   = 0;

    return motor_set_state_locked(id, cfg, target);
}

bool motor_is_running(int id)
{
    bool               running = false;
    const motor_cfg_t *cfg     = NULL;
    motor_ctx_t       *ctx     = NULL;

    pthread_mutex_lock(&s_mutex);
    if (motor_require_runtime_cfg_locked(id, &ctx, &cfg) == SW_OK)
    {
        running = motor_is_running_state(ctx->state);
    }
    pthread_mutex_unlock(&s_mutex);
    return running;
}

uint16_t motor_get_current(int id)
{
    uint16_t           current = 0U;
    const motor_cfg_t *cfg     = NULL;
    motor_ctx_t       *ctx     = NULL;

    pthread_mutex_lock(&s_mutex);
    if (motor_require_runtime_cfg_locked(id, &ctx, &cfg) == SW_OK)
    {
        current = ctx->load_current;
    }
    pthread_mutex_unlock(&s_mutex);
    return current;
}

sw_err_t motor_hold_gear(int id, motor_gear_t gear)
{
    sw_err_t ret;

    if (gear < MOTOR_GEAR_1)
    {
        return SW_ERR_PARAM;
    }

    pthread_mutex_lock(&s_mutex);
    ret = motor_require_action_cfg_locked(id, MOTOR_ACTION_HOLD, false, NULL);
    if (ret != SW_OK)
    {
        pthread_mutex_unlock(&s_mutex);
        return ret;
    }

    ret = motor_gear_pend_locked(id, MOTOR_STATE_HOLD, (int8_t)gear);
    pthread_mutex_unlock(&s_mutex);
    return ret;
}

sw_err_t motor_move_gear(int id, motor_gear_t gear, bool fwd)
{
    sw_err_t ret;
    int8_t   gear_dir;

    if (gear < MOTOR_GEAR_1)
    {
        return SW_ERR_PARAM;
    }

    gear_dir = fwd ? (int8_t)gear : -(int8_t)gear;

    pthread_mutex_lock(&s_mutex);
    ret = motor_require_action_cfg_locked(id, MOTOR_ACTION_MOVE, false, NULL);
    if (ret != SW_OK)
    {
        pthread_mutex_unlock(&s_mutex);
        return ret;
    }

    ret = motor_gear_pend_locked(id, MOTOR_STATE_MOVE, gear_dir);
    pthread_mutex_unlock(&s_mutex);
    return ret;
}
