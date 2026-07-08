/**
 * @file    m8_manual_action.c
 * @brief   M8 手动机构动作统一入口实现
 * @author  HUWANGWEI
 * @date    2026-07-08
 */

#include "projects/m8/adapters/manual/m8_manual_action.h"
#include "projects/m8/adapters/manual/m8_manual_guard.h"
#include "framework/domain/device_control/mechanism/gantry.h"
#include "framework/domain/device_control/mechanism/brush.h"
#include "framework/common/log.h"
#include <stddef.h>

sw_err_t m8_manual_gantry_fwd(int speed_gear)
{
    sw_err_t ret = m8_manual_guard_allow_motion();

    if (ret != SW_OK)
    {
        return ret;
    }

    return gantry_move_fwd(speed_gear, NULL);
}

sw_err_t m8_manual_gantry_rev(int speed_gear)
{
    sw_err_t ret = m8_manual_guard_allow_motion();

    if (ret != SW_OK)
    {
        return ret;
    }

    return gantry_move_rev(speed_gear, NULL);
}

sw_err_t m8_manual_gantry_stop(void)
{
    (void)m8_manual_guard_allow_stop();
    return gantry_stop();
}

sw_err_t m8_manual_brush_start(brush_id_t id, int speed_gear)
{
    sw_err_t ret = m8_manual_guard_allow_motion();

    if (ret != SW_OK)
    {
        return ret;
    }

    return brush_start(id, speed_gear);
}

sw_err_t m8_manual_brush_stop(brush_id_t id)
{
    (void)m8_manual_guard_allow_stop();
    return brush_stop(id);
}
