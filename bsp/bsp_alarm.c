/**
 * @file    bsp_alarm.c
 * @brief   M8 机型报警适配实现
 * @author  HUWANGWEI
 * @date    2026-04-07
 *
 * @note    本文件是机型特定代码的集中地，包含：
 *          ① IO 信号 → 报警码映射（signal_poll，每 10ms 由 svc_alarm 调用）
 *          ② 驱动事件 → 报警码映射（error_event_callback）
 *          ③ 急停复位动作序列（emc_reset）
 *          ④ VFD 故障码读取（vfd_read）
 */

#include "bsp_alarm.h"
#include "bsp_hal.h"
#include "service/svc_alarm.h"
#include "common/log.h"
#include <unistd.h>

/* -------------------------------------------------------------------------
 * ① IO 信号轮询 — 每 10ms 由 svc_alarm 引擎调用
 *    规则：调用 svc_alarm_set_raw_trigger()，让引擎做防抖计时
 * ------------------------------------------------------------------------- */
static void m8_signal_poll(void)
{
    SvcAlarmFlags_t *flags = svc_alarm_flags();

    /* 急停：常闭回路断开则触发（8100，0ms 立即，放第 0 行）*/
    svc_alarm_set_raw_trigger(8100, hal_is_estop_active(), false);

    /* 龙门限位异常：同时触发前限和后限，说明传感器故障（正常只会触发一侧）*/
    bool fwd = hal_gantry_at_fwd_limit();
    bool rev = hal_gantry_at_rev_limit();
    svc_alarm_set_raw_trigger(8001, fwd && !rev, false);  /* 仅前限：暂不报警 */
    svc_alarm_set_raw_trigger(8002, rev && !fwd, false);  /* 仅后限：暂不报警 */

    /* 顶刷升降限位异常 */
    if (flags->is_top_lift_installed) {
        bool up_lim   = hal_top_lift_at_up();
        bool down_lim = hal_top_lift_at_down();
        /* 同时触发两个限位说明传感器异常 */
        svc_alarm_set_raw_trigger(8003, up_lim && down_lim, false);
        svc_alarm_set_raw_trigger(8004, down_lim && up_lim, false);
    }
}

/* -------------------------------------------------------------------------
 * ② 驱动事件回调 — 由 osal 层转发驱动层事件
 *    规则：直接调用 svc_alarm_set_state()（驱动层已经过确认，直接置位）
 * ------------------------------------------------------------------------- */
static void m8_error_event_callback(int code, bool active)
{
    /* code 直接映射到报警码 */
    bool just_notice = false;
    svc_alarm_set_state((uint16_t)code, active, just_notice);
}

/* -------------------------------------------------------------------------
 * ③ 急停复位动作
 * ------------------------------------------------------------------------- */
static void m8_emc_reset(void)
{
    LOG_INFO("bsp_alarm: EMC reset sequence start");

    /* 步骤1：停止所有执行机构 */
    (void)hal_gantry_stop();
    (void)hal_brush_stop();

    /* 步骤2：关闭所有水路 */
    (void)hal_water_pump_set(false);
    (void)hal_water_curtain_set(false);
    (void)hal_water_foam_set(false);
    (void)hal_water_brush_set(false);
    (void)hal_water_highpres_set(false);

    /* 步骤3：复位 VFD 故障 */
    usleep(200U * 1000U);
    (void)hal_gantry_fault_reset();
    (void)hal_brush_fault_reset();

    /* 步骤4：关闭入口（拦截新车进入）*/
    (void)hal_rod_close();
    (void)hal_entry_light_set(ENTRY_LIGHT_RED);

    LOG_INFO("bsp_alarm: EMC reset sequence done");
}

/* -------------------------------------------------------------------------
 * ④ 读取 VFD 故障码（供 svc_alarm 定期轮询）
 * ------------------------------------------------------------------------- */
static int m8_vfd_read(uint16_t *p_code)
{
    uint16_t brush_code  = hal_brush_get_fault_code();
    uint16_t gantry_code = hal_gantry_get_fault_code();

    if (brush_code != 0U) {
        svc_alarm_set_state(8011, true, false);
        LOG_WARN("Brush VFD fault code: %u", (unsigned)brush_code);
    } else {
        svc_alarm_set_state(8011, false, false);
    }

    if (gantry_code != 0U) {
        svc_alarm_set_state(8010, true, false);
        LOG_WARN("Gantry VFD fault code: %u", (unsigned)gantry_code);
    } else {
        svc_alarm_set_state(8010, false, false);
    }

    *p_code = brush_code | gantry_code;
    return 0;
}

/* -------------------------------------------------------------------------
 * 初始化：注册所有回调
 * ------------------------------------------------------------------------- */
sw_err_t bsp_alarm_init(void)
{
    svc_alarm_register_poll_fn(m8_signal_poll);
    svc_alarm_register_emc_reset_fn(m8_emc_reset);
    svc_alarm_register_vfd_read_fn(m8_vfd_read);
    hal_error_callback_regist(m8_error_event_callback);

    LOG_INFO("bsp_alarm init ok");
    return SW_OK;
}
