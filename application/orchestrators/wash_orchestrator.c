/**
 * @file    wash_orchestrator.c
 * @brief   洗车流程编排器（含单步同步执行）
 * @author  HUWANGWEI
 * @date    2026-04-10
 */

#include "application/orchestrators/wash_orchestrator.h"
#include "core/scheduler/thread_registry.h"
#include "service/dev_ctx/dev_ctx.h"
#include "service/svc_param/svc_param.h"
#include "domain/process/recipe.h"
#include "domain/device/unit/brush.h"
#include "domain/device/unit/gantry.h"
#include "domain/device/water.h"
#include "domain/safety/alarm_core.h"
#include "core/event_bus/event_bus.h"
#include "config/threading/thread_config.h"
#include "common/event_types.h"
#include "common/log.h"
#include <semaphore.h>
#include <stdatomic.h>
#include <unistd.h>
#include <sched.h>

#define WASH_STEP_TIMEOUT_MS    120000U
#define STEP_POLL_INTERVAL_MS   50U

static sem_t        s_start_sem;
static atomic_bool  s_busy       = false;
static atomic_bool  s_terminate  = false;
static atomic_bool  s_abort_req  = false;
static wash_mode_t  s_mode       = WASH_MODE_STANDARD;

/* -------------------------------------------------------------------------
 * 单步执行（原 step_engine）
 * ------------------------------------------------------------------------- */
static sw_err_t apply_step(const wash_step_config_t *s, uint16_t brush_freq)
{
    sw_err_t ret;

    LOG_INFO("wash_exec: apply [%s]", s->name);

    if (s->water_prewash)
    {
        (void)water_prewash_on();
    }
    else
    {
        (void)water_prewash_off();
    }

    if (s->water_brush)
    {
        (void)water_brush_on();
    }
    else
    {
        (void)water_brush_off();
    }

    if (s->water_highpres)
    {
        (void)water_highpres_on();
    }
    else
    {
        (void)water_highpres_off();
    }

    if (s->brush_top_on)
    {
        ret = brush_start(BRUSH_ID_TOP, brush_freq);
    }
    else if (s->brush_side_on)
    {
        ret = brush_start(BRUSH_ID_SIDE, brush_freq);
    }
    else
    {
        ret = SW_OK;
        (void)brush_stop();
    }
    if (ret != SW_OK)
    {
        LOG_WARN("wash_exec: brush_start failed ret=%d", (int)ret);
    }

    if (s->gantry_freq > 0U)
    {
        ret = s->gantry_fwd ? gantry_fwd(s->gantry_freq)
                            : gantry_rev(s->gantry_freq);
        if (ret != SW_OK)
        {
            LOG_ERROR("wash_exec: gantry start failed ret=%d", (int)ret);
            return ret;
        }
    }
    else
    {
        (void)gantry_stop();
    }

    return SW_OK;
}

static sw_err_t wait_exit(const wash_step_config_t *s, uint32_t timeout_ms)
{
    uint32_t elapsed_ms = 0U;

    if ((s->step == WASH_STEP_ENTRY) || (s->step == WASH_STEP_COMPLETE))
    {
        return SW_OK;
    }

    while (true)
    {
        if (atomic_load(&s_abort_req))
        {
            LOG_WARN("wash_exec: aborted at [%s]", s->name);
            return SW_ERR_STATE;
        }

        if (alarm_core_has_error())
        {
            LOG_ERROR("wash_exec: ERROR alarm at [%s]", s->name);
            return SW_ERR_STATE;
        }

        if (s->exit_at_fwd_limit && gantry_at_fwd_limit())
        {
            LOG_INFO("wash_exec: [%s] exit - fwd limit", s->name);
            break;
        }
        if (s->exit_at_rev_limit && gantry_at_rev_limit())
        {
            LOG_INFO("wash_exec: [%s] exit - rev limit", s->name);
            break;
        }
        if ((s->exit_pos_pulse >= 0) &&
            (gantry_get_pos() >= (int32_t)s->exit_pos_pulse))
        {
            LOG_INFO("wash_exec: [%s] exit - pos reached", s->name);
            break;
        }

        usleep((unsigned long)STEP_POLL_INTERVAL_MS * 1000UL);
        elapsed_ms += STEP_POLL_INTERVAL_MS;

        if (elapsed_ms >= timeout_ms)
        {
            LOG_ERROR("wash_exec: [%s] timeout after %u ms",
                      s->name, (unsigned)timeout_ms);
            return SW_ERR_TIMEOUT;
        }
    }

    return SW_OK;
}

sw_err_t wash_exec_step(const wash_step_config_t *step,
                        uint32_t timeout_ms,
                        uint16_t brush_freq)
{
    sw_err_t ret;

    if (step == NULL)
    {
        return SW_ERR_PARAM;
    }

    ret = apply_step(step, brush_freq);
    if (ret != SW_OK)
    {
        return ret;
    }

    return wait_exit(step, timeout_ms);
}

void wash_exec_clear_abort(void)
{
    atomic_store(&s_abort_req, false);
}

/* -------------------------------------------------------------------------
 * worker_thread
 * ------------------------------------------------------------------------- */
static void wash_stop_all_outputs(void)
{
    (void)gantry_stop();
    (void)brush_off();
    (void)water_all_off();
}

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

        const wash_step_config_t *steps;
        int                       count;

        if (recipe_get(s_mode, &steps, &count) != SW_OK)
        {
            LOG_ERROR("wash_worker: recipe_get failed mode=%d", (int)s_mode);
            (void)event_publish(EVT_WASH_ABORTED, (uint32_t)SW_ERR_PARAM);
            atomic_store(&s_busy, false);
            continue;
        }

        uint16_t brush_freq = (uint16_t)svc_param_get_int(
            PARAM_KEY_BRUSH_FREQ_TOP, 4500);

        wash_exec_clear_abort();
        dev_ctx_set_wash_progress(WASH_STEP_IDLE, s_mode);

        LOG_INFO("wash_worker: start mode=%d steps=%d freq=%u",
                 (int)s_mode, count, (unsigned)brush_freq);

        sw_err_t result = SW_OK;
        for (int i = 0; i < count; i++)
        {
            dev_ctx_set_wash_progress(steps[i].step, s_mode);
            result = wash_exec_step(&steps[i], WASH_STEP_TIMEOUT_MS, brush_freq);
            if (result != SW_OK)
            {
                LOG_ERROR("wash_worker: step[%d] failed ret=%d", i, (int)result);
                break;
            }
        }

        wash_stop_all_outputs();

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
 * 对外接口
 * ------------------------------------------------------------------------- */
sw_err_t wash_orchestrator_init(void)
{
    atomic_store(&s_busy,      false);
    atomic_store(&s_terminate, false);
    atomic_store(&s_abort_req, false);

    if (sem_init(&s_start_sem, 0, 0) != 0)
    {
        LOG_ERROR("wash_orchestrator_init: sem_init failed");
        return SW_ERR_HW;
    }

    sw_err_t ret = thread_register("wash_worker", wash_worker_fn,
                                   SCHED_OTHER, 0, THD_WASH_WORKER_STACK);
    if (ret != SW_OK)
    {
        return ret;
    }

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
    atomic_store(&s_abort_req, true);
    wash_stop_all_outputs();
    LOG_WARN("wash_orchestrator: abort");
}

bool wash_orchestrator_is_busy(void)
{
    return (bool)atomic_load(&s_busy);
}
