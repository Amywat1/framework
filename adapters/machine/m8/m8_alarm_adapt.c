/**
 * @file    m8_alarm_adapt.c
 * @brief   M8 机型报警适配（非 DI 类采集 / 急停复位序列）
 * @author  胡望伟
 * @date    2026-04-10
 */

#include "adapters/machine/m8/m8_alarm_adapt.h"
#include "adapters/machine/m8/m8_feature_map.h"
#include "adapters/machine/m8/m8_signal_filter.h"
#include "domain/model/alarm_code.h"
#include "domain/device/water.h"
#include "domain/safety/alarm_core.h"
#include "ports/hal/hal_motion_port.h"
#include "ports/hal/hal_indicator_port.h"
#include "ports/hal/hal_vfd_port.h"
#include "common/log.h"
#include "common/sw_error.h"
#include <unistd.h>

/* M8 刷子电流异常检测阈值 */
#define M8_BRUSH_CURRENT_HIGH_MA        800U
#define M8_BRUSH_CURRENT_LOW_MA         50U
/* 以 VFD 慢速轮询事件计数代替绝对毫秒；VFD_SLOW_POLL_MS ≈ 2000ms/event */
#define M8_BRUSH_CURRENT_DELAY_EVENTS   1U   /* 启动后等待 1 次 event（≈2s）再监测 */
#define M8_BRUSH_CURRENT_CONFIRM_EVENTS 2U   /* 连续 2 次异常事件（≈4s ≥ 原 3s 阈值）触发告警 */

/* 文件级 static，便于 m8_alarm_adapt_init 重置（Fix-6）*/
static uint8_t s_brush_startup_events = 0U;   /* 刷子启动后收到的 CURRENT_UPDATE 事件计数 */
static uint8_t s_brush_anomaly_events = 0U;   /* 连续异常事件计数 */

/**
 * @brief  双限位同时触发时判定为限位异常
 */
static void m8_sync_combo_limit_alarms(void)
{
    bool dual_gantry = m8_signal_is_active(M8_SIG_GANTRY_FWD_LIM)
                    && m8_signal_is_active(M8_SIG_GANTRY_REV_LIM);

    alarm_core_set_state(ALARM_CODE_GANTRY_FWD_LIM, dual_gantry, false);
    alarm_core_set_state(ALARM_CODE_GANTRY_REV_LIM, dual_gantry, false);

#if M8_FEAT_TOP_LIFT_INSTALLED
    {
        bool dual_lift = m8_signal_is_active(M8_SIG_LIFT_UP_LIM)
                      && m8_signal_is_active(M8_SIG_LIFT_DOWN_LIM);

        alarm_core_set_state(ALARM_CODE_LIFT_UP_LIM, dual_lift, false);
        alarm_core_set_state(ALARM_CODE_LIFT_DOWN_LIM, dual_lift, false);
    }
#else
    alarm_core_set_raw_trigger(ALARM_CODE_LIFT_UP_LIM, false, true);
    alarm_core_set_raw_trigger(ALARM_CODE_LIFT_DOWN_LIM, false, true);
#endif
}

static void m8_signal_poll(void)
{
    /* VFD 故障码和电流检测已由 drv_vfd monitor worker 内部轮询，
     * 状态变化通过 event_cb → m8_alarm_on_vfd_fault/current_update 主动上报。
     * 此处无需 Modbus IO，io_poll 路径纯 DI 采集。*/

    alarm_core_set_raw_trigger(ALARM_CODE_ESTOP,
                               m8_signal_is_active(M8_SIG_ESTOP),
                               false);

    m8_sync_combo_limit_alarms();

    alarm_core_set_raw_trigger(ALARM_CODE_PUMP_DRY_RUN,
                               water_is_pump_on() && !water_is_any_valve_open(),
                               false);
}

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

void m8_alarm_on_vfd_fault(bool is_brush, bool has_fault)
{
    uint16_t code = is_brush ? ALARM_CODE_VFD_BRUSH : ALARM_CODE_VFD_GANTRY;

    alarm_core_set_state(code, has_fault, false);
    if (has_fault)
    {
        LOG_WARN("m8_alarm_adapt: %s VFD fault detected", is_brush ? "brush" : "gantry");
    }
    else
    {
        LOG_INFO("m8_alarm_adapt: %s VFD fault cleared", is_brush ? "brush" : "gantry");
    }
}

void m8_alarm_on_vfd_current_update(bool is_brush)
{
#ifndef BUILD_SIM
    const hal_vfd_ops_t *vfd = hal_vfd_get_ops();
    uint16_t             current;

    if (!is_brush || (vfd == NULL) || (vfd->get_state == NULL) || (vfd->read_current == NULL))
    {
        return;
    }

    /* 仅刷子正转时检测 */
    if (vfd->get_state(HAL_VFD_BRUSH) != HAL_VFD_STATE_FWD)
    {
        s_brush_startup_events = 0U;
        s_brush_anomaly_events = 0U;
        return;
    }

    /* 启动后等待 DELAY_EVENTS 次 event 再开始监测 */
    if (s_brush_startup_events < M8_BRUSH_CURRENT_DELAY_EVENTS)
    {
        s_brush_startup_events++;
        s_brush_anomaly_events = 0U;
        return;
    }

    /* read_current 现在返回 drv_vfd 缓存值，无 Modbus IO */
    if (vfd->read_current(HAL_VFD_BRUSH, &current) != SW_OK)
    {
        return;
    }

    if ((current > M8_BRUSH_CURRENT_HIGH_MA) || (current < M8_BRUSH_CURRENT_LOW_MA))
    {
        s_brush_anomaly_events++;
        if (s_brush_anomaly_events >= M8_BRUSH_CURRENT_CONFIRM_EVENTS)
        {
            const hal_motion_ops_t *motion = hal_motion_get_ops();

            LOG_WARN("m8_alarm_adapt: brush current anomaly %u mA", (unsigned)current);
            alarm_core_set_state(ALARM_CODE_BRUSH_CURRENT, true, false);
            if ((motion != NULL) && (motion->brush_stop != NULL))
            {
                (void)motion->brush_stop();
            }
            s_brush_anomaly_events = 0U;
        }
    }
    else
    {
        s_brush_anomaly_events = 0U;
    }
#else
    (void)is_brush;
#endif
}

sw_err_t m8_alarm_adapt_init(void)
{
    /* 重置跨调用状态（Fix-6：防止测试多次 init 时状态污染）*/
    s_brush_startup_events = 0U;
    s_brush_anomaly_events = 0U;

    alarm_core_register_poll_fn(m8_signal_poll);
    alarm_core_register_emc_reset_fn(m8_emc_reset);

    LOG_INFO("m8_alarm_adapt: init ok");
    return SW_OK;
}
