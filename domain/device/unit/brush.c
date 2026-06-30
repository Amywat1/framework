/**
 * @file    brush.c
 * @brief   刷子设备实现（顶刷 / 侧刷选择与电机启停）
 * @author  HUWANGWEI
 * @date    2026-04-10
 *
 * @note    接触器切换由 motor 层 pre_start 回调异步完成，本模块无阻塞操作。
 */

#include "domain/device/unit/brush.h"
#include "domain/device/actuator/motor/motor.h"
#include "machines/m8/config/m8_motor_table.h"
#include "common/log.h"

static brush_id_t s_active_brush = BRUSH_ID_NONE;
static bool       s_is_running   = false;
static bool       s_initialized  = false;

sw_err_t brush_init(void)
{
    s_active_brush = BRUSH_ID_NONE;
    s_is_running   = false;
    s_initialized  = true;
    LOG_INFO("brush: init ok");
    return SW_OK;
}

sw_err_t brush_start(brush_id_t id, uint16_t freq_hz)
{
    sw_err_t ret;

    if (!s_initialized)
    {
        return SW_ERR_NOT_INIT;
    }
    if (id == BRUSH_ID_NONE)
    {
        return SW_ERR_PARAM;
    }
    if (freq_hz == 0U)
    {
        return brush_stop();
    }

    /* 先更新选中的刷子 ID，motor 的 pre_start 回调读取此值来决定切换哪路接触器 */
    s_active_brush = id;

    ret = motor_hold(MOTOR_BRUSH, (int)freq_hz);
    if (ret != SW_OK)
    {
        LOG_ERROR("brush: motor_hold failed ret=%d", (int)ret);
        return ret;
    }

    s_is_running = true;
    LOG_INFO("brush: %s start requested freq=%u",
             (id == BRUSH_ID_TOP) ? "top" : "side",
             (unsigned)freq_hz);
    return SW_OK;
}

sw_err_t brush_stop(void)
{
    sw_err_t ret;

    if (!s_is_running)
    {
        return SW_OK;
    }

    ret = motor_stop(MOTOR_BRUSH);
    if (ret != SW_OK)
    {
        LOG_WARN("brush: motor_stop failed ret=%d", (int)ret);
        return ret;
    }

    s_is_running = false;
    LOG_INFO("brush: stopped");
    return SW_OK;
}

sw_err_t brush_off(void)
{
    sw_err_t ret;

    ret = motor_stop(MOTOR_BRUSH);
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

bool brush_is_fault(void)
{
    return motor_get_state(MOTOR_BRUSH) == MOTOR_STATE_FAULT;
}

uint16_t brush_get_current(void)
{
    return motor_get_current(MOTOR_BRUSH);
}
