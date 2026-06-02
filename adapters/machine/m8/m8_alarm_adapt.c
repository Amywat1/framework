/**
 * @file    m8_alarm_adapt.c
 * @brief   M8 机型报警适配（非 DI 类采集 / 急停复位序列）
 * @author  胡望伟
 * @date    2026-04-10
 *
 * @note    DI 类信号由 m8_signal_filter 统一滤波并写入 alarm_core；
 *          本文件仅负责 VFD 故障直报、水泵空转检测与急停复位动作。
 */

#include "adapters/machine/m8/m8_alarm_adapt.h"
#include "domain/model/alarm_code.h"
#include "domain/device/water.h"
#include "domain/safety/alarm_core.h"
#include "ports/hal/hal_motion_port.h"
#include "ports/hal/hal_indicator_port.h"
#include "ports/hal/hal_sensor_port.h"
#include "common/log.h"
#include "common/sw_error.h"
#include <unistd.h>

#ifndef BUILD_SIM
#include "adapters/hal/linux_hw/m8_hal_ctx.h"
#include "adapters/hal/linux_hw/m8_vfd_control.h"
#include "adapters/hal/linux_hw/drv/drv_vfd.h"
#endif

#define M8_BRUSH_CURRENT_HIGH_MA        800U
#define M8_BRUSH_CURRENT_LOW_MA         50U
#define M8_BRUSH_CURRENT_DELAY_MS       2000U
#define M8_BRUSH_CURRENT_CONFIRM_MS     3000U

#ifndef BUILD_SIM
/**
 * @brief  刷子 VFD 电流监测（原由 motor 层承担，现改由机型适配轮询）
 */
static void m8_brush_current_poll(void)
{
    static uint32_t s_run_ms      = 0U;
    static uint32_t s_anomaly_ms  = 0U;
    uint16_t        current       = 0U;
    sw_err_t        ret;

    if (!m8_vfd_brush_is_running())
    {
        s_run_ms     = 0U;
        s_anomaly_ms = 0U;
        return;
    }

    s_run_ms += (uint32_t)ALARM_POLL_PERIOD_MS;
    if (s_run_ms < M8_BRUSH_CURRENT_DELAY_MS)
    {
        s_anomaly_ms = 0U;
        return;
    }

    ret = drv_vfd_read_current(m8_ctx_vfd_brush(), &current);
    if (ret != SW_OK)
    {
        return;
    }

    if ((current > M8_BRUSH_CURRENT_HIGH_MA) || (current < M8_BRUSH_CURRENT_LOW_MA))
    {
        s_anomaly_ms += (uint32_t)ALARM_POLL_PERIOD_MS;
        if (s_anomaly_ms >= M8_BRUSH_CURRENT_CONFIRM_MS)
        {
            const hal_motion_ops_t *motion = hal_motion_get_ops();

            LOG_WARN("m8_alarm_adapt: brush current anomaly %u mA", (unsigned)current);
            alarm_core_set_state(ALARM_CODE_BRUSH_CURRENT, true, false);
            if ((motion != NULL) && (motion->brush_stop != NULL))
            {
                (void)motion->brush_stop();
            }
            s_anomaly_ms = 0U;
        }
    }
    else
    {
        s_anomaly_ms = 0U;
    }
}
#endif /* BUILD_SIM */

/**
 * @brief  非 DI 类报警轮询（由 alarm_core_tick_ms 每周期调用）
 */
static void m8_signal_poll(void)
{
    const hal_sensor_ops_t *sensor = hal_sensor_get_ops();

    if ((sensor != NULL) && (sensor->poll_input_events != NULL))
    {
        sensor->poll_input_events();
    }

    /* VFD 故障直报 */
    if (sensor != NULL)
    {
        uint16_t code = 0U;

        if (sensor->get_vfd_fault_code(HAL_VFD_GANTRY, &code) == SW_OK)
        {
            bool has_fault = (code != 0U);

            alarm_core_set_state(ALARM_CODE_VFD_GANTRY, has_fault, false);
            if (has_fault)
            {
                LOG_WARN("m8_alarm_adapt: gantry VFD fault code=0x%04X", (unsigned)code);
            }
        }

        code = 0U;
        if (sensor->get_vfd_fault_code(HAL_VFD_BRUSH, &code) == SW_OK)
        {
            bool has_fault = (code != 0U);

            alarm_core_set_state(ALARM_CODE_VFD_BRUSH, has_fault, false);
            if (has_fault)
            {
                LOG_WARN("m8_alarm_adapt: brush VFD fault code=0x%04X", (unsigned)code);
            }
        }
    }

    alarm_core_set_raw_trigger(ALARM_CODE_PUMP_DRY_RUN,
                               water_is_pump_on() && !water_is_any_valve_open(),
                               false);

#ifndef BUILD_SIM
    m8_brush_current_poll();
#endif
}

/**
 * @brief  急停复位动作序列（由 alarm_core_manual_reset 调用）
 */
static void m8_emc_reset(void)
{
    const hal_motion_ops_t    *motion    = hal_motion_get_ops();
    const hal_indicator_ops_t *indicator = hal_indicator_get_ops();

    LOG_INFO("m8_alarm_adapt: EMC reset sequence start");

    if (motion != NULL)
    {
        (void)motion->gantry_stop();
        (void)motion->brush_stop();
    }

    (void)water_all_off();

    usleep(200U * 1000U);

    if (motion != NULL)
    {
        (void)motion->gantry_fault_reset();
        (void)motion->brush_fault_reset();
    }

    if (indicator != NULL)
    {
        (void)indicator->rod_close();
        (void)indicator->entry_light_set(HAL_LIGHT_RED);
    }

    LOG_INFO("m8_alarm_adapt: EMC reset sequence done");
}

void m8_alarm_on_vfd_comm_lost(bool is_brush)
{
    uint16_t code = is_brush ? ALARM_CODE_MODBUS_BRUSH : ALARM_CODE_MODBUS_GANTRY;

    alarm_core_set_state(code, true, false);
    LOG_WARN("m8_alarm_adapt: %s VFD Modbus comm lost", is_brush ? "brush" : "gantry");
}

void m8_alarm_on_vfd_comm_restored(bool is_brush)
{
    uint16_t code = is_brush ? ALARM_CODE_MODBUS_BRUSH : ALARM_CODE_MODBUS_GANTRY;

    alarm_core_set_state(code, false, false);
    LOG_INFO("m8_alarm_adapt: %s VFD Modbus comm restored", is_brush ? "brush" : "gantry");
}

sw_err_t m8_alarm_adapt_init(void)
{
    alarm_core_register_poll_fn(m8_signal_poll);
    alarm_core_register_emc_reset_fn(m8_emc_reset);

    LOG_INFO("m8_alarm_adapt: init ok");
    return SW_OK;
}
