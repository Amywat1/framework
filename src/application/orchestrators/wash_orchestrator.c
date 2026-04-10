/**
 * @file    wash_orchestrator.c
 * @brief   洗车流程编排器实现
 * @author  胡望伟
 * @date    2026-04-10
 */

#include "application/orchestrators/wash_orchestrator.h"
#include "core/scheduler/thread_registry.h"
#include "service/dev_ctx/dev_ctx.h"
#include "service/svc_param/svc_param.h"
#include "domain/process/step_engine.h"
#include "domain/process/recipe.h"
#include "domain/device/gantry.h"
#include "domain/device/brush.h"
#include "domain/device/top_lift.h"
#include "domain/device/water.h"
#include "domain/safety/alarm_core.h"
#include "core/event_bus/event_bus.h"
#include "config/threading/thread_config.h"
#include "common/event_types.h"
#include "common/log.h"
#include <semaphore.h>
#include <stdatomic.h>
#include <unistd.h>
#include <pthread.h>
#include <sched.h>

/* 单步最长超时（ms）*/
#define WASH_STEP_TIMEOUT_MS    120000U

/* -------------------------------------------------------------------------
 * 内部状态
 * ------------------------------------------------------------------------- */
static sem_t        s_start_sem;
static atomic_bool  s_busy      = false;
static atomic_bool  s_terminate = false;
static wash_mode_t  s_mode      = WASH_MODE_STANDARD;

/* -------------------------------------------------------------------------
 * worker_thread 函数
 * ------------------------------------------------------------------------- */
static void *wash_worker_fn(void *arg)
{
    (void)arg;

    while (true)
    {
        sem_wait(&s_start_sem);

        if (atomic_load(&s_terminate))
        {
            break;
        }

        /* 获取配方 */
        const wash_step_config_t *steps;
        int                       count;
        if (recipe_get(s_mode, &steps, &count) != SW_OK)
        {
            LOG_ERROR("wash_worker: recipe_get failed mode=%d", (int)s_mode);
            (void)event_publish(EVT_WASH_ABORTED, (uint32_t)SW_ERR_PARAM);
            atomic_store(&s_busy, false);
            continue;
        }

        /* 刷子频率（顶刷/侧刷共用，实际可按步骤区分）*/
        uint16_t brush_freq = (uint16_t)svc_param_get_int(
            PARAM_KEY_BRUSH_FREQ_TOP, 4500);

        step_engine_clear_abort();
        dev_ctx_set_wash_progress(WASH_STEP_IDLE, s_mode);

        LOG_INFO("wash_worker: start mode=%d steps=%d freq=%u",
                 (int)s_mode, count, (unsigned)brush_freq);

        sw_err_t result = SW_OK;
        int      i;
        for (i = 0; i < count; i++)
        {
            dev_ctx_set_wash_progress(steps[i].step, s_mode);
            result = step_engine_exec_step(&steps[i],
                                           WASH_STEP_TIMEOUT_MS,
                                           brush_freq);
            if (result != SW_OK)
            {
                LOG_ERROR("wash_worker: step[%d] failed ret=%d", i, (int)result);
                break;
            }
        }

        /* 洗车结束：关停所有执行机构 */
        (void)gantry_stop();
        (void)brush_off();
        (void)water_all_off();
        (void)top_lift_up_start(0U); /* 归位顶刷，异步 */

        dev_ctx_set_wash_progress(WASH_STEP_IDLE, s_mode);

        if (result == SW_OK)
        {
            (void)event_publish(EVT_WASH_DONE, 0U);
            LOG_INFO("wash_worker: wash done");
        }
        else
        {
            (void)event_publish(EVT_WASH_ABORTED, (uint32_t)result);
            LOG_WARN("wash_worker: wash aborted ret=%d", (int)result);
        }

        atomic_store(&s_busy, false);
    }

    LOG_INFO("wash_worker: thread exit");
    return NULL;
}

/* -------------------------------------------------------------------------
 * 事件处理：LOCKOUT → 中止当前步骤
 * ------------------------------------------------------------------------- */
static void on_safety_lockout(const event_t *evt)
{
    (void)evt;
    if (atomic_load(&s_busy))
    {
        step_engine_abort();
        LOG_WARN("wash_orchestrator: abort requested by LOCKOUT");
    }
}

/* -------------------------------------------------------------------------
 * 接口实现
 * ------------------------------------------------------------------------- */
sw_err_t wash_orchestrator_init(void)
{
    sw_err_t ret;

    atomic_store(&s_busy,      false);
    atomic_store(&s_terminate, false);

    if (sem_init(&s_start_sem, 0, 0) != 0)
    {
        LOG_ERROR("wash_orchestrator_init: sem_init failed");
        return SW_ERR_HW;
    }

    ret = step_engine_init();
    if (ret != SW_OK) { return ret; }

    ret = event_subscribe(EVT_SAFETY_LOCKOUT, on_safety_lockout);
    if (ret != SW_OK) { return ret; }

    /* 注册 worker_thread 到调度器（由 scheduler_start_all 创建）*/
    ret = thread_register("wash_worker", wash_worker_fn,
                          SCHED_OTHER, 0, THD_WASH_WORKER_STACK);
    if (ret != SW_OK) { return ret; }

    LOG_INFO("wash_orchestrator: init ok");
    return SW_OK;
}

sw_err_t wash_orchestrator_start(wash_mode_t mode)
{
    if (atomic_load(&s_busy))
    {
        LOG_WARN("wash_orchestrator_start: busy");
        return SW_ERR_BUSY;
    }
    if (alarm_core_has_error())
    {
        LOG_ERROR("wash_orchestrator_start: ERROR alarm active");
        return SW_ERR_STATE;
    }

    s_mode = mode;
    atomic_store(&s_busy, true);
    sem_post(&s_start_sem);
    return SW_OK;
}

void wash_orchestrator_abort(void)
{
    if (atomic_load(&s_busy))
    {
        step_engine_abort();
        LOG_WARN("wash_orchestrator: abort");
    }
}

bool wash_orchestrator_is_busy(void)
{
    return (bool)atomic_load(&s_busy);
}
