/**
 * @file    step_engine.c
 * @brief   洗车单步同步执行器实现
 * @author  胡望伟
 * @date    2026-04-10
 */

#include "domain/process/step_engine.h"
#include "domain/device/unit/brush.h"
#include "domain/device/unit/gantry.h"
#include "domain/device/unit/top_lift.h"
#include "domain/device/water.h"
#include "domain/safety/alarm_core.h"
#include "common/log.h"
#include <stdatomic.h>
#include <unistd.h>

/* 退出条件轮询间隔（ms）*/
#define STEP_POLL_INTERVAL_MS   50U

/* 等待升降限位超时（ms，升降动作先于其他执行机构）*/
#define LIFT_WAIT_TIMEOUT_MS    15000U

static atomic_bool s_abort_req = false;

/* -------------------------------------------------------------------------
 * 内部：等待升降到位（用于 top_lift_down 步骤中先降后走的约束）
 * ------------------------------------------------------------------------- */
static sw_err_t wait_lift_bottom(void)
{
    uint32_t elapsed_ms = 0U;

    while (!top_lift_at_bottom())
    {
        if (atomic_load(&s_abort_req))
        {
            return SW_ERR_STATE;
        }
        usleep((unsigned long)STEP_POLL_INTERVAL_MS * 1000UL);
        elapsed_ms += STEP_POLL_INTERVAL_MS;
        if (elapsed_ms >= LIFT_WAIT_TIMEOUT_MS)
        {
            LOG_ERROR("step_engine: wait_lift_bottom timeout");
            return SW_ERR_TIMEOUT;
        }
    }
    return SW_OK;
}

/* -------------------------------------------------------------------------
 * 内部：应用步骤配置（启动执行机构）
 * ------------------------------------------------------------------------- */
static sw_err_t apply_step(const wash_step_config_t *s, uint16_t brush_freq)
{
    sw_err_t ret;

    LOG_INFO("step_engine: apply [%s]", s->name);

    /* 1. 水路 */
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

    /* 2. 顶刷升降（先于刷子和龙门：下降到位后再走）*/
    if (s->top_lift_down)
    {
        ret = top_lift_down_start(0U); /* 0 = HAL 默认脉冲数 */
        if (ret != SW_OK)
        {
            LOG_ERROR("step_engine: top_lift_down_start failed ret=%d", (int)ret);
            return ret;
        }
        /* 兜底保护：等待下限位生效。 */
        ret = wait_lift_bottom();
        if (ret != SW_OK)
        {
            LOG_ERROR("step_engine: wait_lift_bottom failed ret=%d", (int)ret);
            return ret;
        }
    }
    else
    {
        /* 顶刷升起（异步，不等待完成，上升过程中可同步走龙门）*/
        (void)top_lift_up_start(0U);
    }

    /* 3. 刷子 */
    if (s->brush_top_on)
    {
        ret = brush_start(BRUSH_ID_TOP, brush_freq);
        if (ret != SW_OK)
        {
            LOG_WARN("step_engine: brush_start TOP failed ret=%d", (int)ret);
        }
    }
    else if (s->brush_side_on)
    {
        ret = brush_start(BRUSH_ID_SIDE, brush_freq);
        if (ret != SW_OK)
        {
            LOG_WARN("step_engine: brush_start SIDE failed ret=%d", (int)ret);
        }
    }
    else
    {
        (void)brush_stop();
    }

    /* 4. 龙门 */
    if (s->gantry_freq > 0U)
    {
        ret = s->gantry_fwd ? gantry_fwd(s->gantry_freq)
                            : gantry_rev(s->gantry_freq);
        if (ret != SW_OK)
        {
            LOG_ERROR("step_engine: gantry start failed ret=%d", (int)ret);
            return ret;
        }
    }
    else
    {
        (void)gantry_stop();
    }

    return SW_OK;
}

/* -------------------------------------------------------------------------
 * 内部：等待步骤退出条件
 * ------------------------------------------------------------------------- */
static sw_err_t wait_exit(const wash_step_config_t *s, uint32_t timeout_ms)
{
    uint32_t elapsed_ms = 0U;

    /* 入场和完成步骤：无需等待，立即通过 */
    if ((s->step == WASH_STEP_ENTRY) || (s->step == WASH_STEP_COMPLETE))
    {
        return SW_OK;
    }

    while (true)
    {
        /* 检查中止信号 */
        if (atomic_load(&s_abort_req))
        {
            LOG_WARN("step_engine: aborted at [%s]", s->name);
            return SW_ERR_STATE;
        }

        /* 检查 ERROR 报警（不包括 NOTICE）*/
        if (alarm_core_has_error())
        {
            LOG_ERROR("step_engine: ERROR alarm triggered at [%s]", s->name);
            return SW_ERR_STATE;
        }

        /* 检查退出条件 */
        if (s->exit_at_fwd_limit && gantry_at_fwd_limit())
        {
            LOG_INFO("step_engine: [%s] exit - fwd limit", s->name);
            break;
        }
        if (s->exit_at_rev_limit && gantry_at_rev_limit())
        {
            LOG_INFO("step_engine: [%s] exit - rev limit", s->name);
            break;
        }
        if ((s->exit_pos_pulse >= 0) &&
            (gantry_get_pos() >= (int32_t)s->exit_pos_pulse))
        {
            LOG_INFO("step_engine: [%s] exit - pos reached", s->name);
            break;
        }

        usleep((unsigned long)STEP_POLL_INTERVAL_MS * 1000UL);
        elapsed_ms += STEP_POLL_INTERVAL_MS;

        if (elapsed_ms >= timeout_ms)
        {
            LOG_ERROR("step_engine: [%s] timeout after %u ms",
                      s->name, (unsigned)timeout_ms);
            return SW_ERR_TIMEOUT;
        }
    }

    return SW_OK;
}

/* -------------------------------------------------------------------------
 * 接口实现
 * ------------------------------------------------------------------------- */
sw_err_t step_engine_init(void)
{
    atomic_store(&s_abort_req, false);
    LOG_INFO("step_engine: init ok");
    return SW_OK;
}

sw_err_t step_engine_exec_step(const wash_step_config_t *step,
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

void step_engine_abort(void)
{
    atomic_store(&s_abort_req, true);
}

void step_engine_clear_abort(void)
{
    atomic_store(&s_abort_req, false);
}
