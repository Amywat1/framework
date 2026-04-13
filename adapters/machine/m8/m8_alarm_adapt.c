/**
 * @file    m8_alarm_adapt.c
 * @brief   M8 机型报警适配（IO 轮询 / VFD 故障直报 / 急停复位回调）
 * @author  胡望伟
 * @date    2026-04-10
 *
 * @note    本文件是机型特定代码的集中地：
 *          ① m8_signal_poll()：
 *              - 编码器事件轮询
 *              - IO 组合报警（如双限位同时触发）
 *              - 直接调用 get_vfd_fault_code() 读取 Modbus 故障寄存器，
 *                通过 alarm_core_set_state() 进入安全域（直报路径），并复用同次读取结果发布硬件事件
 *              - Modbus 通信丢失/恢复由 drv_vfd 的事件回调链路单独处理
 *          ② m8_emc_reset()：急停复位序列（停机 → 关水 → 复位驱动 → 关闭入口）
 *          初始化时将以上回调注册到 alarm_core。
 *
 *          依赖：alarm_core（domain）、hal_sensor/motion/water/indicator（ports）
 */

#include "domain/safety/alarm_core.h"
#include "domain/model/alarm_code.h"
#include "ports/hal/hal_sensor_port.h"
#include "ports/hal/hal_motion_port.h"
#include "ports/hal/hal_water_port.h"
#include "ports/hal/hal_indicator_port.h"
#include "adapters/machine/m8/m8_feature_map.h"
#include "core/event_bus/event_bus.h"
#include "common/log.h"
#include "common/sw_error.h"
#include <unistd.h>

/* -------------------------------------------------------------------------
 * VFD 故障事件去重状态
 * 仅在“无故障 -> 有故障”时发布 EVT_HW_VFD_*_FAULT，避免持续故障期间重复刷事件。
 * ------------------------------------------------------------------------- */
static bool s_gantry_vfd_fault_reported = false;
static bool s_brush_vfd_fault_reported  = false;

/* -------------------------------------------------------------------------
 * ① IO 信号轮询（每 ALARM_POLL_PERIOD_MS 由 alarm_core_tick_ms 调用）
 * ------------------------------------------------------------------------- */
static void m8_signal_poll(void)
{
    const hal_sensor_ops_t *sensor = hal_sensor_get_ops();

    /* 输入事件链路：
     *   - 真机：这里只提取编码器边沿，急停/限位由 m8_signal_filter_tick() 统一防抖并发布
     *   - 仿真：保留原有 poll_input_events 逻辑，便于测试工具直接注入状态
     */
    sensor->poll_input_events();

    /* 真机中，单路急停/限位已由 m8_signal_filter_tick() 统一同步到 alarm_core。 */
#ifdef BUILD_SIM
    /* 仿真构建未接入 drv_io 信号滤波链路，这里保留单路原始状态同步。 */
    alarm_core_set_raw_trigger(ALARM_CODE_ESTOP,
                               sensor->is_estop_active(), false);
    alarm_core_set_raw_trigger(ALARM_CODE_GANTRY_FWD_LIM,
                               sensor->gantry_at_fwd_limit(), false);
    alarm_core_set_raw_trigger(ALARM_CODE_GANTRY_REV_LIM,
                               sensor->gantry_at_rev_limit(), false);
#if M8_FEAT_TOP_LIFT_INSTALLED
    alarm_core_set_raw_trigger(ALARM_CODE_LIFT_UP_LIM,
                               sensor->lift_at_top(), false);
    alarm_core_set_raw_trigger(ALARM_CODE_LIFT_DOWN_LIM,
                               sensor->lift_at_bottom(), false);
#else
    alarm_core_set_raw_trigger(ALARM_CODE_LIFT_UP_LIM, false, true);
    alarm_core_set_raw_trigger(ALARM_CODE_LIFT_DOWN_LIM, false, true);
#endif
#else
    /* 真机中仅处理多信号组合逻辑。 */
    bool fwd = sensor->gantry_at_fwd_limit();
    bool rev = sensor->gantry_at_rev_limit();
    alarm_core_set_state(ALARM_CODE_GANTRY_FWD_LIM, (fwd && rev), false);
    alarm_core_set_state(ALARM_CODE_GANTRY_REV_LIM, (fwd && rev), false);

#if M8_FEAT_TOP_LIFT_INSTALLED
    bool up_lim   = sensor->lift_at_top();
    bool down_lim = sensor->lift_at_bottom();
    alarm_core_set_state(ALARM_CODE_LIFT_UP_LIM,   (up_lim && down_lim), false);
    alarm_core_set_state(ALARM_CODE_LIFT_DOWN_LIM, (up_lim && down_lim), false);
#else
    /* 升降未安装：降级为 NOTICE，不触发停机 */
    alarm_core_set_raw_trigger(ALARM_CODE_LIFT_UP_LIM, false, true);
    alarm_core_set_raw_trigger(ALARM_CODE_LIFT_DOWN_LIM, false, true);
#endif
#endif

    /* VFD 故障直报：
     * Modbus 通信丢失/恢复报警由 drv_vfd → m8_hal_ctx → alarm_core 负责，
     * 此处仅负责读取故障码并更新 VFD 故障状态。 */
    {
        uint16_t code = 0U;
        if (sensor->get_vfd_fault_code(HAL_VFD_GANTRY, &code) == SW_OK)
        {
            bool has_fault = (code != 0U);

            alarm_core_set_state(ALARM_CODE_VFD_GANTRY, has_fault, false);
            if (has_fault)
            {
                if (!s_gantry_vfd_fault_reported)
                {
                    LOG_WARN("m8_alarm_adapt: gantry VFD fault code=0x%04X", (unsigned)code);
                    (void)event_publish(EVT_HW_VFD_GANTRY_FAULT, ALARM_CODE_VFD_GANTRY);
                    s_gantry_vfd_fault_reported = true;
                }
            }
            else
            {
                s_gantry_vfd_fault_reported = false;
            }
        }
        /* 通信失败时保持上一次 VFD 故障状态，避免误清除。 */
    }
    {
        uint16_t code = 0U;
        if (sensor->get_vfd_fault_code(HAL_VFD_BRUSH, &code) == SW_OK)
        {
            bool has_fault = (code != 0U);

            alarm_core_set_state(ALARM_CODE_VFD_BRUSH, has_fault, false);
            if (has_fault)
            {
                if (!s_brush_vfd_fault_reported)
                {
                    LOG_WARN("m8_alarm_adapt: brush VFD fault code=0x%04X", (unsigned)code);
                    (void)event_publish(EVT_HW_VFD_BRUSH_FAULT, ALARM_CODE_VFD_BRUSH);
                    s_brush_vfd_fault_reported = true;
                }
            }
            else
            {
                s_brush_vfd_fault_reported = false;
            }
        }
        /* 通信失败时保持上一次 VFD 故障状态，避免误清除。 */
    }
}

/* -------------------------------------------------------------------------
 * ② 急停复位动作序列（由 alarm_core_manual_reset 调用）
 * ------------------------------------------------------------------------- */
static void m8_emc_reset(void)
{
    const hal_motion_ops_t    *motion    = hal_motion_get_ops();
    const hal_water_ops_t     *water     = hal_water_get_ops();
    const hal_indicator_ops_t *indicator = hal_indicator_get_ops();

    LOG_INFO("m8_alarm_adapt: EMC reset sequence start");

    /* 步骤1：停止所有执行机构 */
    (void)motion->gantry_stop();
    (void)motion->brush_stop();

    /* 步骤2：关闭所有水路 */
    (void)water->all_off();

    /* 步骤3：等待 VFD 完全停止后复位故障 */
    usleep(200U * 1000U);
    (void)motion->gantry_fault_reset();
    (void)motion->brush_fault_reset();

    /* 步骤4：关闭入口（禁止新车进入，灯红色）*/
    (void)indicator->rod_close();
    (void)indicator->entry_light_set(HAL_LIGHT_RED);

    LOG_INFO("m8_alarm_adapt: EMC reset sequence done");
}

/* -------------------------------------------------------------------------
 * 初始化：注册回调到 alarm_core
 * ------------------------------------------------------------------------- */
sw_err_t m8_alarm_adapt_init(void)
{
    s_gantry_vfd_fault_reported = false;
    s_brush_vfd_fault_reported  = false;

    alarm_core_register_poll_fn(m8_signal_poll);
    alarm_core_register_emc_reset_fn(m8_emc_reset);

    LOG_INFO("m8_alarm_adapt: init ok");
    return SW_OK;
}
