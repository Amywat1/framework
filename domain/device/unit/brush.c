/**
 * @file    brush.c
 * @brief   刷子设备实现
 * @author  胡望伟
 * @date    2026-04-10
 */

#include "domain/device/unit/brush.h"
#include "domain/safety/interlock.h"
#include "common/log.h"

static brush_id_t s_active_brush = BRUSH_ID_NONE;
static bool       s_is_running   = false;

sw_err_t brush_init(void)
{
    s_active_brush = BRUSH_ID_NONE;
    s_is_running   = false;
    LOG_INFO("brush: init ok");
    return SW_OK;
}

sw_err_t brush_start(brush_id_t id, uint16_t freq_hz)
{
    sw_err_t ret;

    if (id == BRUSH_ID_NONE)
    {
        return SW_ERR_PARAM;
    }
    if (freq_hz == 0U)
    {
        return brush_stop();
    }

    ret = interlock_check_motion(MOTION_TYPE_BRUSH_SWITCH);
    if (ret != SW_OK)
    {
        return ret;
    }

    (void)id;
    (void)freq_hz;
    return SW_ERR_NOT_SUPPORT;
}

sw_err_t brush_stop(void)
{
    if (!s_is_running)
    {
        return SW_OK;
    }

    s_is_running = false;
    LOG_INFO("brush: stopped");
    return SW_OK;
}

sw_err_t brush_off(void)
{
    s_active_brush = BRUSH_ID_NONE;
    s_is_running   = false;
    LOG_INFO("brush: off (state reset)");
    return SW_OK;
}

bool brush_is_running(brush_id_t id)
{
    return s_is_running && (s_active_brush == id);
}

brush_id_t brush_get_active(void)
{
    return s_active_brush;
}
