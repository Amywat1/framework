/**
 * @file    gantry.c
 * @brief   龙门行走设备实现
 * @author  胡望伟
 * @date    2026-04-10
 */

#include "domain/device/gantry.h"
#include "domain/safety/interlock.h"
#include "ports/hal/hal_motion_port.h"
#include "ports/hal/hal_sensor_port.h"
#include "core/event_bus/event_bus.h"
#include "common/event_types.h"
#include "common/log.h"
#include <stdatomic.h>

/*
 * 位置来源：直接委托给 hal_sensor_get_ops()->get_gantry_pos()（Option A）。
 * HAL 层（m8_hal_ctx.c）通过编码器回调原子累加，是位置的唯一可信来源。
 * 本层不维护冗余计数，避免双源竞争。
 */
static atomic_bool s_homing = false; /* 正在归位中 */

/* -------------------------------------------------------------------------
 * 事件处理（由 event_dispatch_thread 调用）
 * ------------------------------------------------------------------------- */
static void on_gantry_rev_limit(const event_t *evt)
{
    (void)evt;

    if (!atomic_load(&s_homing))
    {
        return; /* 非归位状态，忽略 */
    }

    /* 归位完成：停龙门，清零位置，发布完成事件 */
    const hal_motion_ops_t *ops   = hal_motion_get_ops();
    const hal_sensor_ops_t *s_ops = hal_sensor_get_ops();
    (void)ops->gantry_stop();
    s_ops->reset_gantry_pos();

    atomic_store(&s_homing, false);

    (void)event_publish(EVT_COMP_HOME_DONE, 0U);
    LOG_INFO("gantry: home done (rev limit event)");
}

/* -------------------------------------------------------------------------
 * 接口实现
 * ------------------------------------------------------------------------- */
sw_err_t gantry_init(void)
{
    atomic_store(&s_homing, false);

    sw_err_t ret = event_subscribe(EVT_HW_GANTRY_REV_LIM, on_gantry_rev_limit);
    if (ret != SW_OK)
    {
        LOG_ERROR("gantry_init: subscribe EVT_HW_GANTRY_REV_LIM failed");
        return ret;
    }

    LOG_INFO("gantry: init ok");
    return SW_OK;
}

sw_err_t gantry_fwd(uint16_t freq_hz)
{
    sw_err_t ret = interlock_check_motion(MOTION_TYPE_GANTRY_FWD);
    if (ret != SW_OK)
    {
        return ret;
    }
    LOG_INFO("gantry: fwd freq=%u", (unsigned)freq_hz);
    return hal_motion_get_ops()->gantry_fwd(freq_hz);
}

sw_err_t gantry_rev(uint16_t freq_hz)
{
    sw_err_t ret = interlock_check_motion(MOTION_TYPE_GANTRY_REV);
    if (ret != SW_OK)
    {
        return ret;
    }
    LOG_INFO("gantry: rev freq=%u", (unsigned)freq_hz);
    return hal_motion_get_ops()->gantry_rev(freq_hz);
}

sw_err_t gantry_stop(void)
{
    const hal_motion_ops_t *ops = hal_motion_get_ops();
    return ops->gantry_stop();
}

sw_err_t gantry_home_start(uint16_t freq_hz)
{
    const hal_motion_ops_t  *m_ops = hal_motion_get_ops();
    const hal_sensor_ops_t  *s_ops = hal_sensor_get_ops();
    sw_err_t                 ret;

    ret = interlock_check_motion(MOTION_TYPE_GANTRY_REV);
    if (ret != SW_OK)
    {
        return ret;
    }

    if (atomic_load(&s_homing))
    {
        LOG_WARN("gantry_home_start: already homing");
        return SW_ERR_BUSY;
    }

    /* 若已在后限位，立即完成 */
    if (s_ops->gantry_at_rev_limit())
    {
        s_ops->reset_gantry_pos();
        (void)event_publish(EVT_COMP_HOME_DONE, 0U);
        LOG_INFO("gantry_home_start: already at home");
        return SW_OK;
    }

    atomic_store(&s_homing, true);

    ret = m_ops->gantry_rev(freq_hz);
    if (ret != SW_OK)
    {
        atomic_store(&s_homing, false);
        LOG_ERROR("gantry_home_start: gantry_rev failed ret=%d", (int)ret);
        return ret;
    }

    LOG_INFO("gantry_home_start: homing started freq=%u", (unsigned)freq_hz);
    return SW_OK;
}

bool gantry_at_fwd_limit(void)
{
    return hal_sensor_get_ops()->gantry_at_fwd_limit();
}

bool gantry_at_rev_limit(void)
{
    return hal_sensor_get_ops()->gantry_at_rev_limit();
}

int32_t gantry_get_pos(void)
{
    /* Option A：位置由 HAL（m8_hal_ctx.c）通过编码器原子累加维护，直接读取 */
    return hal_sensor_get_ops()->get_gantry_pos();
}

void gantry_reset_pos(void)
{
    hal_sensor_get_ops()->reset_gantry_pos();
}
