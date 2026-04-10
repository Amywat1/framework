/**
 * @file    app_wash_engine.c
 * @brief   龙门洗车流程引擎实现
 * @author  胡望伟
 * @date    2026-04-08
 */

#include "app_wash_engine.h"
#include "app_wash_steps.h"
#include "component/comp_brush.h"
#include "component/comp_gantry.h"
#include "component/comp_top_lift.h"
#include "component/comp_water.h"
#include "service/svc_alarm.h"
#include "service/svc_param.h"
#include "common/log.h"
#include <pthread.h>
#include <unistd.h>
#include <stdatomic.h>

/* -------------------------------------------------------------------------
 * 内部状态
 * ------------------------------------------------------------------------- */
static pthread_t   s_wash_thread;
static atomic_bool s_is_running = false;
static atomic_bool s_stop_req   = false;
static atomic_int  s_cur_step   = WASH_STEP_IDLE;
static WashMode_t  s_mode       = WASH_MODE_STANDARD;

/* 单步最长超时（ms）*/
#define STEP_TIMEOUT_MS     120000U

/* -------------------------------------------------------------------------
 * 内部：应用单步配置（启动所有执行机构）
 * ------------------------------------------------------------------------- */
static sw_err_t apply_step(const WashStepConfig_t *s)
{
    sw_err_t ret;

    LOG_INFO("wash_engine: step [%s]", s->name);
    atomic_store(&s_cur_step, (int)s->step);

    /* 水路 */
    if (s->water_prewash) {
        (void)comp_water_prewash_on();
    } else {
        (void)comp_water_prewash_off();
    }
    if (s->water_brush) {
        (void)comp_water_brush_on();
    } else {
        (void)comp_water_brush_off();
    }
    if (s->water_highpres) {
        (void)comp_water_highpres_on();
    } else {
        (void)comp_water_highpres_off();
    }

    /* 顶刷升降 */
    if (s->top_lift_down) {
        ret = comp_top_lift_down(0U);
        if (ret != SW_OK) {
            LOG_ERROR("wash_engine: top_lift_down failed ret=%d", (int)ret);
        }
    } else {
        ret = comp_top_lift_up(10000U);
        if (ret != SW_OK) {
            LOG_WARN("wash_engine: top_lift_up timeout, continue");
        }
    }

    /* 刷子 */
    if (s->brush_top_on) {
        uint16_t freq = (uint16_t)svc_param_get_int(PARAM_KEY_BRUSH_FREQ_TOP, 4500);
        (void)comp_brush_start(BRUSH_ID_TOP, freq);
    } else if (s->brush_side_on) {
        uint16_t freq = (uint16_t)svc_param_get_int(PARAM_KEY_BRUSH_FREQ_SIDE, 4500);
        (void)comp_brush_start(BRUSH_ID_SIDE, freq);
    } else {
        (void)comp_brush_stop();
    }

    /* 龙门移动 */
    if (s->gantry_freq > 0U) {
        ret = s->gantry_fwd ? comp_gantry_fwd(s->gantry_freq)
                            : comp_gantry_rev(s->gantry_freq);
        if (ret != SW_OK) {
            return ret;
        }
    } else {
        (void)comp_gantry_stop();
    }

    return SW_OK;
}

/* -------------------------------------------------------------------------
 * 内部：等待步骤退出条件
 * ------------------------------------------------------------------------- */
static sw_err_t wait_exit(const WashStepConfig_t *s)
{
    uint32_t elapsed_ms = 0U;

    /* 入场和完成步骤：立即通过 */
    if ((s->step == WASH_STEP_ENTRY) || (s->step == WASH_STEP_COMPLETE)) {
        return SW_OK;
    }

    while (true) {
        if (atomic_load(&s_stop_req)) {
            return SW_ERR_STATE;
        }
        if (svc_alarm_has_error()) {
            LOG_ERROR("wash_engine: alarm triggered at step [%s]", s->name);
            return SW_ERR_STATE;
        }
        if (s->exit_at_fwd_limit && comp_gantry_at_fwd_limit()) {
            break;
        }
        if (s->exit_at_rev_limit && comp_gantry_at_rev_limit()) {
            break;
        }
        if ((s->exit_pos_pulse >= 0) &&
            (comp_gantry_get_pos() >= s->exit_pos_pulse)) {
            break;
        }

        usleep(50U * 1000U);
        elapsed_ms += 50U;

        if (elapsed_ms >= STEP_TIMEOUT_MS) {
            LOG_ERROR("wash_engine: step [%s] timeout", s->name);
            return SW_ERR_TIMEOUT;
        }
    }

    return SW_OK;
}

/* -------------------------------------------------------------------------
 * 洗车线程
 * ------------------------------------------------------------------------- */
static void *wash_thread_fn(void *arg)
{
    int                     count = 0;
    const WashStepConfig_t *steps = wash_steps_get(s_mode, &count);
    sw_err_t                ret;
    int                     i;

    (void)arg;

    LOG_INFO("wash_engine: start mode=%d steps=%d", (int)s_mode, count);

    for (i = 0; i < count; i++) {
        if (atomic_load(&s_stop_req)) {
            break;
        }
        ret = apply_step(&steps[i]);
        if (ret != SW_OK) {
            LOG_ERROR("wash_engine: apply_step[%d] failed", i);
            break;
        }
        ret = wait_exit(&steps[i]);
        if (ret != SW_OK) {
            LOG_ERROR("wash_engine: wait_exit[%d] failed ret=%d", i, (int)ret);
            break;
        }
    }

    /* 洗车结束（完成或中断）：停止所有执行机构 */
    (void)comp_gantry_stop();
    (void)comp_brush_off();
    (void)comp_water_all_off();
    (void)comp_top_lift_up(10000U);

    atomic_store(&s_cur_step,   (int)WASH_STEP_IDLE);
    atomic_store(&s_is_running, false);

    LOG_INFO("wash_engine: thread exit");
    return NULL;
}

/* -------------------------------------------------------------------------
 * 接口实现
 * ------------------------------------------------------------------------- */
sw_err_t wash_engine_init(void)
{
    atomic_store(&s_is_running, false);
    atomic_store(&s_stop_req,   false);
    atomic_store(&s_cur_step,   (int)WASH_STEP_IDLE);
    LOG_INFO("wash_engine init ok");
    return SW_OK;
}

sw_err_t wash_engine_start(WashMode_t mode)
{
    if (atomic_load(&s_is_running)) {
        LOG_WARN("wash_engine_start: already running");
        return SW_ERR_BUSY;
    }
    if (svc_alarm_has_error()) {
        LOG_ERROR("wash_engine_start: active error alarm, rejected");
        return SW_ERR_STATE;
    }

    s_mode = mode;
    atomic_store(&s_stop_req,   false);
    atomic_store(&s_is_running, true);

    if (pthread_create(&s_wash_thread, NULL, wash_thread_fn, NULL) != 0) {
        atomic_store(&s_is_running, false);
        LOG_ERROR("wash_engine_start: pthread_create failed");
        return SW_ERR_HW;
    }
    pthread_detach(s_wash_thread);
    return SW_OK;
}

void wash_engine_emergency_stop(void)
{
    atomic_store(&s_stop_req, true);
    (void)comp_gantry_stop();
    (void)comp_brush_off();
    (void)comp_water_all_off();
    LOG_WARN("wash_engine: emergency stop");
}

bool wash_engine_is_done(void)
{
    return !atomic_load(&s_is_running);
}

WashStep_t wash_engine_get_step(void)
{
    return (WashStep_t)atomic_load(&s_cur_step);
}
