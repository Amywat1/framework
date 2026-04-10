/**
 * @file    bsp_hal.c
 * @brief   硬件抽象接口实现
 * @author  HUWANGWEI
 * @date    2026-04-07
 */

#include "bsp_hal.h"
#include "driver/drv_io.h"
#include "driver/drv_vfd.h"
#include "driver/drv_stepper.h"
#include "common/log.h"
#include "config/machine_config.h"
#include <unistd.h>
#include <string.h>
#include <stdio.h>

static void (*s_error_cb)(int code, bool active) = NULL;

/* 两个 VFD 实例（由 bsp_hal 持有，对外不暴露）*/
static drv_vfd_t s_vfd_brush;
static drv_vfd_t s_vfd_gantry;

/* -------------------------------------------------------------------------
 * 驱动事件路由（从 driver 层上报，转发到 bsp_alarm 的回调）
 * ------------------------------------------------------------------------- */
static void brush_event_cb(int event_code)
{
    if (s_error_cb != NULL) {
        s_error_cb(event_code, true);
    }
}

static void gantry_event_cb(int event_code)
{
    if (s_error_cb != NULL) {
        s_error_cb(event_code, true);
    }
}

/* IO 子板在线状态变化（掉线/恢复）→ 路由到报警系统
 * error code 8020 预留给 IO 板通信故障 */
static void io_board_error_cb(int board_id, bool offline)
{
    (void)board_id;
    if (s_error_cb != NULL) {
        s_error_cb(8020, offline);
    }
}

/* -------------------------------------------------------------------------
 * 初始化
 * ------------------------------------------------------------------------- */
sw_err_t hal_init(void)
{
    sw_err_t ret;

    ret = drv_io_init();
    if (ret != SW_OK) {
        LOG_ERROR("hal_init: drv_io_init failed");
        return ret;
    }
    drv_io_register_board_error_cb(io_board_error_cb);

    /* 刷子 VFD：Modbus 地址 1，仅正转，无反转引脚 */
    ret = drv_vfd_init(&s_vfd_brush,
                       CFG_VFD_BRUSH_SERIAL_PORT, CFG_VFD_BRUSH_BAUD,
                       CFG_VFD_BRUSH_MODBUS_ADDR,
                       DO_SIDE_BRUSH_FWD, false, DO_SIDE_BRUSH_FWD,
                       DO_SIDE_BRUSH_RST);
    if (ret != SW_OK) {
        LOG_ERROR("hal_init: vfd_brush init failed");
        return ret;
    }
    drv_vfd_register_event_cb(&s_vfd_brush, brush_event_cb);

    /* 接触器上电安全状态：全部断开 */
    (void)drv_io_do_set(DO_TOP_BRUSH_ACT,  false);
    (void)drv_io_do_set(DO_SIDE_BRUSH_ACT, false);

    /* 龙门 VFD：Modbus 地址 2，支持正反转 */
    ret = drv_vfd_init(&s_vfd_gantry,
                       CFG_VFD_GANTRY_SERIAL_PORT, CFG_VFD_GANTRY_BAUD,
                       CFG_VFD_GANTRY_MODBUS_ADDR,
                       DO_GANTRY_FWD, true, DO_GANTRY_REV,
                       DO_GANTRY_RST);
    if (ret != SW_OK) {
        LOG_ERROR("hal_init: vfd_gantry init failed");
        return ret;
    }
    drv_vfd_register_event_cb(&s_vfd_gantry, gantry_event_cb);

    ret = drv_stepper_init();
    if (ret != SW_OK) {
        LOG_ERROR("hal_init: drv_stepper_init failed");
        return ret;
    }

    LOG_INFO("hal_init ok");
    return SW_OK;
}

/* -------------------------------------------------------------------------
 * IO 操作
 * ------------------------------------------------------------------------- */
sw_err_t hal_do_set(drv_io_do_t pin, bool val) { return drv_io_do_set(pin, val); }
bool     hal_di_read(drv_io_di_t pin)           { return drv_io_di_read(pin); }
void     hal_register_input_cb(void (*cb)(int di_num, bool state)) {
    drv_io_register_input_cb(cb);
}

bool hal_is_estop_active(void)
{
    /* 急停为常闭回路，DI13=false（断开）时急停有效 */
    return !drv_io_di_read(DI_ESTOP);
}

/* -------------------------------------------------------------------------
 * 入口指示灯
 * ------------------------------------------------------------------------- */
sw_err_t hal_entry_light_set(hal_entry_light_t state)
{
    (void)drv_io_do_set(DO_ENTRY_GREEN1, false);
    (void)drv_io_do_set(DO_ENTRY_GREEN2, false);
    (void)drv_io_do_set(DO_ENTRY_RED,    false);
    (void)drv_io_do_set(DO_ENTRY_YELLOW, false);

    switch (state) {
        case ENTRY_LIGHT_GREEN:
            (void)drv_io_do_set(DO_ENTRY_GREEN1, true);
            (void)drv_io_do_set(DO_ENTRY_GREEN2, true);
            break;
        case ENTRY_LIGHT_RED:
            (void)drv_io_do_set(DO_ENTRY_RED, true);
            break;
        case ENTRY_LIGHT_YELLOW:
            (void)drv_io_do_set(DO_ENTRY_YELLOW, true);
            break;
        case ENTRY_LIGHT_OFF:
        default:
            break;
    }
    return SW_OK;
}

/* -------------------------------------------------------------------------
 * 入口挡杆
 * ------------------------------------------------------------------------- */
sw_err_t hal_rod_open(void)
{
    (void)drv_io_do_set(DO_ROD_EXTEND,  false);
    (void)drv_io_do_set(DO_ROD_RETRACT, true);
    return SW_OK;
}

sw_err_t hal_rod_close(void)
{
    (void)drv_io_do_set(DO_ROD_RETRACT, false);
    (void)drv_io_do_set(DO_ROD_EXTEND,  true);
    return SW_OK;
}

/* -------------------------------------------------------------------------
 * 刷子 VFD
 * 接触器切换逻辑在此处（板级知识：DO_TOP_BRUSH_ACT / DO_SIDE_BRUSH_ACT）
 * ------------------------------------------------------------------------- */
sw_err_t hal_brush_select(hal_brush_sel_t sel)
{
    if (drv_vfd_get_state(&s_vfd_brush) == DRV_VFD_STATE_FWD) {
        LOG_ERROR("hal_brush_select: VFD still running, stop first");
        return SW_ERR_STATE;
    }

    /* 先断开全部接触器，等待延时，再吸合目标接触器 */
    (void)drv_io_do_set(DO_TOP_BRUSH_ACT,  false);
    (void)drv_io_do_set(DO_SIDE_BRUSH_ACT, false);
    usleep(200U * 1000U);   /* 200ms 防止两个接触器同时吸合 */

    if (sel == HAL_BRUSH_SEL_TOP) {
        (void)drv_io_do_set(DO_TOP_BRUSH_ACT, true);
    } else {
        (void)drv_io_do_set(DO_SIDE_BRUSH_ACT, true);
    }

    LOG_INFO("hal_brush_select: sel=%d ok", (int)sel);
    return SW_OK;
}

sw_err_t hal_brush_run(uint16_t freq_hz)    { return drv_vfd_run_fwd(&s_vfd_brush, freq_hz); }
sw_err_t hal_brush_stop(void)               { return drv_vfd_stop(&s_vfd_brush); }
sw_err_t hal_brush_fault_reset(void)        { return drv_vfd_fault_reset(&s_vfd_brush); }
uint16_t hal_brush_get_fault_code(void)
{
    uint16_t code = 0U;
    (void)drv_vfd_get_fault_code(&s_vfd_brush, &code);
    return code;
}

/* -------------------------------------------------------------------------
 * 龙门 VFD
 * ------------------------------------------------------------------------- */
sw_err_t hal_gantry_fwd(uint16_t freq_hz)   { return drv_vfd_run_fwd(&s_vfd_gantry, freq_hz); }
sw_err_t hal_gantry_rev(uint16_t freq_hz)   { return drv_vfd_run_rev(&s_vfd_gantry, freq_hz); }
sw_err_t hal_gantry_stop(void)              { return drv_vfd_stop(&s_vfd_gantry); }
sw_err_t hal_gantry_fault_reset(void)       { return drv_vfd_fault_reset(&s_vfd_gantry); }
uint16_t hal_gantry_get_fault_code(void)
{
    uint16_t code = 0U;
    (void)drv_vfd_get_fault_code(&s_vfd_gantry, &code);
    return code;
}

bool hal_gantry_at_fwd_limit(void) { return drv_io_di_read(DI_GANTRY_FWD_LIMIT); }
bool hal_gantry_at_rev_limit(void) { return drv_io_di_read(DI_GANTRY_REAR_LIMIT); }

/* -------------------------------------------------------------------------
 * 顶刷升降
 * ------------------------------------------------------------------------- */
sw_err_t hal_top_lift_up(uint32_t pulses)
{
    (void)drv_stepper_enable();
    sw_err_t ret = drv_stepper_move(pulses, STEPPER_DIR_UP,
                                    CFG_STEPPER_PULSE_US);
    (void)drv_stepper_disable();
    return ret;
}

sw_err_t hal_top_lift_down(uint32_t pulses)
{
    (void)drv_stepper_enable();
    sw_err_t ret = drv_stepper_move(pulses, STEPPER_DIR_DOWN,
                                    CFG_STEPPER_PULSE_US);
    (void)drv_stepper_disable();
    return ret;
}

bool hal_top_lift_at_up(void)   { return drv_io_di_read(DI_TOP_LIFT_UP); }
bool hal_top_lift_at_down(void) { return drv_io_di_read(DI_TOP_LIFT_DOWN); }

/* -------------------------------------------------------------------------
 * 水路
 * ------------------------------------------------------------------------- */
sw_err_t hal_water_pump_set(bool on)     { return drv_io_do_set(DO_WATER_PUMP,     on); }
sw_err_t hal_water_curtain_set(bool on)  { return drv_io_do_set(DO_WATER_CURTAIN,  on); }
sw_err_t hal_water_foam_set(bool on)     { return drv_io_do_set(DO_WATER_FOAM,     on); }
sw_err_t hal_water_brush_set(bool on)    { return drv_io_do_set(DO_WATER_BRUSH,    on); }
sw_err_t hal_water_highpres_set(bool on) { return drv_io_do_set(DO_WATER_HIGHPRES, on); }

/* -------------------------------------------------------------------------
 * 驱动事件回调注册
 * ------------------------------------------------------------------------- */
void hal_error_callback_regist(void (*cb)(int code, bool active))
{
    s_error_cb = cb;
}

/* -------------------------------------------------------------------------
 * CLI 调试接口
 * ------------------------------------------------------------------------- */
int bsp_debug_ctl(char *cmd, char *p1, char *p2)
{
    if (cmd == NULL) {
        return 0;
    }

    if (strcmp(cmd, "do") == 0) {
        int pin = atoi(p1);
        int val = atoi(p2);
        (void)drv_io_do_set((drv_io_do_t)pin, (bool)val);
        LOG_INFO("bsp debug: DO%d = %d", pin, val);
        return 1;
    }

    if (strcmp(cmd, "di") == 0) {
        int pin = atoi(p1);
        bool state = drv_io_di_read((drv_io_di_t)pin);
        LOG_INFO("bsp debug: DI%d = %d", pin, (int)state);
        return 1;
    }

    if (strcmp(cmd, "gantry_fwd") == 0) {
        uint16_t freq = (uint16_t)atoi(p1);
        (void)hal_gantry_fwd(freq);
        return 1;
    }

    if (strcmp(cmd, "gantry_stop") == 0) {
        (void)hal_gantry_stop();
        return 1;
    }

    return 0;
}
