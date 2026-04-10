/**
 * @file    bsp_hal.h
 * @brief   硬件抽象接口（M8 机型唯一硬件访问入口）
 * @author  HUWANGWEI
 * @date    2026-04-07
 *
 * @note    上层（component 及以上）所有硬件操作通过此层，
 *          禁止直接调用 driver 层函数。
 */

#ifndef BSP_HAL_H
#define BSP_HAL_H

#ifdef __cplusplus
extern "C" {
#endif

#include "common/sw_types.h"
#include "common/sw_error.h"
#include "driver/drv_io.h"

/* -------------------------------------------------------------------------
 * 初始化
 * ------------------------------------------------------------------------- */

/**
 * @brief  初始化所有驱动（IO 子板、两路 VFD、步进电机）
 * @retval SW_OK / SW_ERR_HW
 */
sw_err_t hal_init(void);

/* -------------------------------------------------------------------------
 * IO 操作（透传 drv_io，提供语义化别名）
 * ------------------------------------------------------------------------- */
sw_err_t hal_do_set(drv_io_do_t pin, bool val);
bool     hal_di_read(drv_io_di_t pin);
void     hal_register_input_cb(void (*cb)(int di_num, bool state));

/* -------------------------------------------------------------------------
 * 急停信号查询（常闭，信号为 false 时急停有效）
 * ------------------------------------------------------------------------- */
bool hal_is_estop_active(void);

/* -------------------------------------------------------------------------
 * 入口指示灯控制
 * ------------------------------------------------------------------------- */
typedef enum
{
    ENTRY_LIGHT_OFF    = 0,
    ENTRY_LIGHT_GREEN,      /* 允许进入：绿灯 */
    ENTRY_LIGHT_RED,        /* 禁止进入：红灯 */
    ENTRY_LIGHT_YELLOW,     /* 等待/注意：黄灯 */
} hal_entry_light_t;

sw_err_t hal_entry_light_set(hal_entry_light_t state);

/* -------------------------------------------------------------------------
 * 入口挡杆控制
 * ------------------------------------------------------------------------- */
sw_err_t hal_rod_open(void);    /* 推杆缩回，放行 */
sw_err_t hal_rod_close(void);   /* 推杆伸出，拦截 */

/* -------------------------------------------------------------------------
 * 刷子选择（板级概念：两路接触器切换共用一台 VFD）
 * ------------------------------------------------------------------------- */
typedef enum
{
    HAL_BRUSH_SEL_TOP  = 0,     /* 顶刷（吸合接触器1，H29）*/
    HAL_BRUSH_SEL_SIDE = 1,     /* 侧刷（吸合接触器2，H28）*/
} hal_brush_sel_t;

/* -------------------------------------------------------------------------
 * 刷子 VFD 操作
 * ------------------------------------------------------------------------- */
sw_err_t hal_brush_select(hal_brush_sel_t sel);
sw_err_t hal_brush_run(uint16_t freq_hz);
sw_err_t hal_brush_stop(void);
sw_err_t hal_brush_fault_reset(void);
uint16_t hal_brush_get_fault_code(void);

/* -------------------------------------------------------------------------
 * 龙门 VFD 操作
 * ------------------------------------------------------------------------- */
sw_err_t hal_gantry_fwd(uint16_t freq_hz);
sw_err_t hal_gantry_rev(uint16_t freq_hz);
sw_err_t hal_gantry_stop(void);
sw_err_t hal_gantry_fault_reset(void);
uint16_t hal_gantry_get_fault_code(void);
bool     hal_gantry_at_fwd_limit(void);
bool     hal_gantry_at_rev_limit(void);

/* -------------------------------------------------------------------------
 * 顶刷升降（透传 drv_stepper）
 * ------------------------------------------------------------------------- */
sw_err_t hal_top_lift_up(uint32_t pulses);
sw_err_t hal_top_lift_down(uint32_t pulses);
bool     hal_top_lift_at_up(void);
bool     hal_top_lift_at_down(void);

/* -------------------------------------------------------------------------
 * 水路控制
 * ------------------------------------------------------------------------- */
sw_err_t hal_water_pump_set(bool on);
sw_err_t hal_water_curtain_set(bool on);
sw_err_t hal_water_foam_set(bool on);
sw_err_t hal_water_brush_set(bool on);
sw_err_t hal_water_highpres_set(bool on);

/* -------------------------------------------------------------------------
 * CLI 调试接口（bsp 命令域）
 * 命令：do <pin> <0|1> / di <pin> / gantry_fwd <freq> / gantry_stop
 * ------------------------------------------------------------------------- */
int bsp_debug_ctl(char *cmd, char *p1, char *p2);

/* -------------------------------------------------------------------------
 * 驱动事件回调注册（供 bsp_alarm 使用）
 * ------------------------------------------------------------------------- */
void hal_error_callback_regist(void (*cb)(int code, bool active));

#ifdef __cplusplus
}
#endif

#endif /* BSP_HAL_H */
