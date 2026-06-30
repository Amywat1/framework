/**
 * @file    brush.c
 * @brief   刷子设备实现（机构级状态管理，无硬件依赖）
 * @author  HUWANGWEI
 * @date    2026-04-10
 *
 * @note    硬件驱动通过 brush_actuator_ops_t 注入，brush.c 不引用任何 HAL 或 motor 符号。
 */

#include "domain/device/mechanism/brush.h"
#include "common/log.h"

static brush_actuator_ops_t s_ops;
static bool                 s_initialized  = false;
static brush_id_t           s_active_brush = BRUSH_ID_NONE;
static bool                 s_is_running   = false;

sw_err_t brush_init(const brush_actuator_ops_t *ops)
{
    if ((ops == NULL) || (ops->start_freq == NULL) || (ops->stop == NULL))
    {
        return SW_ERR_PARAM;
    }

    s_ops          = *ops;
    s_active_brush = BRUSH_ID_NONE;
    s_is_running   = false;
    s_initialized  = true;
    LOG_INFO("brush: init ok");
    return SW_OK;
}

sw_err_t brush_start(brush_id_t id, uint16_t speed_ref)
{
    sw_err_t ret;

    if (!s_initialized)    { return SW_ERR_NOT_INIT; }
    if (id == BRUSH_ID_NONE) { return SW_ERR_PARAM; }
    if (speed_ref == 0U)   { return brush_stop(); }

    /* 先更新选中 ID，ops 实现（m8_brush_setup）在 pre_start 回调中读取此值 */
    s_active_brush = id;

    ret = s_ops.start_freq(id, speed_ref);
    if (ret != SW_OK)
    {
        LOG_ERROR("brush: start_freq id=%d failed ret=%d", (int)id, (int)ret);
        return ret;
    }

    s_is_running = true;
    LOG_INFO("brush: %s start freq=%u", (id == BRUSH_ID_TOP) ? "top" : "side",
             (unsigned)speed_ref);
    return SW_OK;
}

sw_err_t brush_start_gear(brush_id_t id, uint8_t gear)
{
    sw_err_t ret;

    if (!s_initialized)     { return SW_ERR_NOT_INIT; }
    if (id == BRUSH_ID_NONE) { return SW_ERR_PARAM; }
    if (gear == 0U)          { return brush_stop(); }
    if (s_ops.start_gear == NULL) { return SW_ERR_NOT_SUPPORT; }

    s_active_brush = id;

    ret = s_ops.start_gear(id, gear);
    if (ret != SW_OK)
    {
        LOG_ERROR("brush: start_gear id=%d gear=%u failed ret=%d",
                  (int)id, (unsigned)gear, (int)ret);
        return ret;
    }

    s_is_running = true;
    LOG_INFO("brush: %s start gear=%u", (id == BRUSH_ID_TOP) ? "top" : "side",
             (unsigned)gear);
    return SW_OK;
}

sw_err_t brush_stop(void)
{
    sw_err_t ret;

    if (!s_is_running) { return SW_OK; }

    ret = s_ops.stop(s_active_brush);
    if (ret != SW_OK)
    {
        LOG_WARN("brush: stop failed ret=%d", (int)ret);
        return ret;
    }

    s_is_running = false;
    LOG_INFO("brush: stopped");
    return SW_OK;
}

sw_err_t brush_off(void)
{
    sw_err_t ret = SW_OK;

    if (s_active_brush != BRUSH_ID_NONE)
    {
        ret = (s_ops.off != NULL) ? s_ops.off(s_active_brush)
                                  : s_ops.stop(s_active_brush);
    }

    s_is_running   = false;
    s_active_brush = BRUSH_ID_NONE;
    LOG_INFO("brush: off");
    return ret;
}

bool brush_is_running(brush_id_t id)
{
    return s_is_running && (s_active_brush == id);
}

brush_id_t brush_get_active(void)
{
    return s_active_brush;
}

bool brush_is_fault(brush_id_t id)
{
    if (s_ops.is_fault == NULL) { return false; }
    return s_ops.is_fault(id);
}

uint16_t brush_get_current(brush_id_t id)
{
    if (s_ops.get_current == NULL) { return 0U; }
    return s_ops.get_current(id);
}
