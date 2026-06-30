/**
 * @file    brush.c
 * @brief   刷子设备实现（顶刷 / 侧刷 VFD + 接触器管理）
 * @author  HUWANGWEI
 * @date    2026-04-10
 */

#include "domain/device/unit/brush.h"
#include "domain/device/actuator/motor/motor.h"
#include "machines/m8/config/m8_motor_table.h"
#include "common/log.h"

#include <unistd.h>

static brush_cfg_t           s_cfg;
static brush_actuator_ops_t  s_actuator;
static bool                  s_actuator_ready = false;

static brush_id_t s_active_brush = BRUSH_ID_NONE;
static bool       s_is_running   = false;

static void brush_delay_ms(uint32_t ms)
{
    if (ms > 0U)
    {
        usleep((unsigned long)ms * 1000UL);
    }
}

static sw_err_t check_actuator(void)
{
    if (!s_actuator_ready || (s_actuator.set_contactor == NULL))
    {
        return SW_ERR_NOT_INIT;
    }
    return SW_OK;
}

sw_err_t brush_init(const brush_cfg_t *cfg, const brush_actuator_ops_t *ops)
{
    if ((cfg == NULL) || (ops == NULL) || (ops->set_contactor == NULL))
    {
        return SW_ERR_PARAM;
    }

    s_cfg            = *cfg;
    s_actuator       = *ops;
    s_actuator_ready = true;
    s_active_brush   = BRUSH_ID_NONE;
    s_is_running     = false;

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

    ret = check_actuator();
    if (ret != SW_OK)
    {
        LOG_ERROR("brush: start failed, actuator not ready");
        return ret;
    }

    /* 若当前已选中其他刷子，先停 VFD 再切换接触器 */
    if ((s_active_brush != BRUSH_ID_NONE) && (s_active_brush != id))
    {
        (void)motor_stop(MOTOR_BRUSH);
        s_is_running = false;
        brush_delay_ms(s_cfg.contactor_wait_ms);
        (void)s_actuator.set_contactor(s_active_brush, false);
        s_active_brush = BRUSH_ID_NONE;
        brush_delay_ms(s_cfg.contactor_wait_ms);
    }

    /* 若未选中目标刷子，吸合接触器并等待稳定 */
    if (s_active_brush != id)
    {
        ret = s_actuator.set_contactor(id, true);
        if (ret != SW_OK)
        {
            LOG_ERROR("brush: set_contactor id=%d on failed ret=%d", (int)id, (int)ret);
            return ret;
        }
        brush_delay_ms(s_cfg.contactor_wait_ms);
        s_active_brush = id;
    }

    /* 启动电机（刷子仅正转） */
    ret = motor_hold(MOTOR_BRUSH, (int)freq_hz);
    if (ret != SW_OK)
    {
        LOG_ERROR("brush: motor_hold failed ret=%d", (int)ret);
        return ret;
    }

    s_is_running = true;
    LOG_INFO("brush: %s started freq=%u",
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
    sw_err_t first_err = SW_OK;

    ret = motor_stop(MOTOR_BRUSH);
    if ((first_err == SW_OK) && (ret != SW_OK))
    {
        first_err = ret;
    }
    s_is_running = false;

    if (s_actuator_ready && (s_active_brush != BRUSH_ID_NONE))
    {
        brush_delay_ms(s_cfg.contactor_wait_ms);
        ret = s_actuator.set_contactor(s_active_brush, false);
        if ((first_err == SW_OK) && (ret != SW_OK))
        {
            first_err = ret;
        }
    }

    s_active_brush = BRUSH_ID_NONE;
    LOG_INFO("brush: off (state reset)");
    return first_err;
}

bool brush_is_running(brush_id_t id)
{
    return s_is_running && (s_active_brush == id);
}

brush_id_t brush_get_active(void)
{
    return s_active_brush;
}
