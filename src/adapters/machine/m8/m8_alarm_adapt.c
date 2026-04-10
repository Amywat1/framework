/**
 * @file    m8_alarm_adapt.c
 * @brief   M8 机型报警适配（IO 轮询 / VFD 故障直报 / 急停复位回调）
 * @author  胡望伟
 * @date    2026-04-10
 *
 * @note    本文件是机型特定代码的集中地：
 *          ① m8_signal_poll()：
 *              - IO 信号 → alarm_core_set_raw_trigger()（防抖路径）
 *              - 直接调用 get_vfd_fault_code() 读取 Modbus 故障寄存器，
 *                通过 alarm_core_set_state() 进入安全域（直报路径）；
 *              - 额外调用 poll_vfd_faults() 发布硬件事件供其他订阅者感知
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
#include "common/log.h"
#include "common/sw_error.h"
#include <unistd.h>

/* -------------------------------------------------------------------------
 * ① IO 信号轮询（每 ALARM_POLL_PERIOD_MS 由 alarm_core_tick_ms 调用）
 * ------------------------------------------------------------------------- */
static void m8_signal_poll(void)
{
    const hal_sensor_ops_t *sensor = hal_sensor_get_ops();

    /* 急停（常闭，true=有效）*/
    alarm_core_set_raw_trigger(ALARM_CODE_ESTOP,
                               sensor->is_estop_active(), false);

    /* 龙门限位异常：前后限位同时触发说明传感器故障 */
    bool fwd = sensor->gantry_at_fwd_limit();
    bool rev = sensor->gantry_at_rev_limit();
    alarm_core_set_raw_trigger(ALARM_CODE_GANTRY_FWD_LIM, (fwd && rev), false);
    alarm_core_set_raw_trigger(ALARM_CODE_GANTRY_REV_LIM, (rev && fwd), false);

    /* 顶刷升降限位异常（需安装才检测）*/
#if M8_FEAT_TOP_LIFT_INSTALLED
    bool up_lim   = sensor->lift_at_top();
    bool down_lim = sensor->lift_at_bottom();
    /* 两个限位同时触发 = 传感器异常 */
    alarm_core_set_raw_trigger(ALARM_CODE_LIFT_UP_LIM,   (up_lim && down_lim), false);
    alarm_core_set_raw_trigger(ALARM_CODE_LIFT_DOWN_LIM, (down_lim && up_lim), false);
#else
    /* 升降未安装：降级为 NOTICE，不触发停机 */
    alarm_core_set_raw_trigger(ALARM_CODE_LIFT_UP_LIM,   false, true);
    alarm_core_set_raw_trigger(ALARM_CODE_LIFT_DOWN_LIM, false, true);
#endif

    /* VFD 故障直报：直接读取故障码，闭环进入 alarm_core 安全域 */
    {
        uint16_t code = 0U;
        sw_err_t ret  = sensor->get_vfd_fault_code(HAL_VFD_GANTRY, &code);
        if (ret == SW_OK)
        {
            /* 通信成功：清除 Modbus 超时报警，更新 VFD 故障状态 */
            alarm_core_set_state(ALARM_CODE_MODBUS_GANTRY, false, false);
            alarm_core_set_state(ALARM_CODE_VFD_GANTRY, (code != 0U), false);
            if (code != 0U)
            {
                LOG_WARN("m8_alarm_adapt: gantry VFD fault code=0x%04X", (unsigned)code);
            }
        }
        else
        {
            /* Modbus 通信失败：触发通信超时报警，VFD 故障状态保持上一次值 */
            alarm_core_set_state(ALARM_CODE_MODBUS_GANTRY, true, false);
        }
    }
    {
        uint16_t code = 0U;
        sw_err_t ret  = sensor->get_vfd_fault_code(HAL_VFD_BRUSH, &code);
        if (ret == SW_OK)
        {
            /* 通信成功：清除 Modbus 超时报警，更新 VFD 故障状态 */
            alarm_core_set_state(ALARM_CODE_MODBUS_BRUSH, false, false);
            alarm_core_set_state(ALARM_CODE_VFD_BRUSH, (code != 0U), false);
            if (code != 0U)
            {
                LOG_WARN("m8_alarm_adapt: brush VFD fault code=0x%04X", (unsigned)code);
            }
        }
        else
        {
            alarm_core_set_state(ALARM_CODE_MODBUS_BRUSH, true, false);
        }
    }

    /* 额外发布硬件事件（供 event_bus 其他订阅者感知，如 dev_ctx 更新）*/
    sensor->poll_vfd_faults();
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
    alarm_core_register_poll_fn(m8_signal_poll);
    alarm_core_register_emc_reset_fn(m8_emc_reset);

    LOG_INFO("m8_alarm_adapt: init ok");
    return SW_OK;
}
