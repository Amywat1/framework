/**
 * @file    drv_stepper.h
 * @brief   雷赛步进电机驱动接口（脉冲/方向控制，通过 IO 子板数字输出）
 * @author  HUWANGWEI
 * @date    2026-04-07
 *
 * @note    引脚对应关系：
 *          DO7  (DO_TOP_LIFT_ENA) → 步进驱动器 ENA-
 *          DO8  (DO_TOP_LIFT_DIR) → 步进驱动器 DIR-
 *          H26  (DO_TOP_LIFT_PUL) → 步进驱动器 PUL-
 *          ENA 低电平使能，高电平锁定（根据雷赛驱动器具体配置确认）
 */

#ifndef DRV_STEPPER_H
#define DRV_STEPPER_H

#include "common/sw_types.h"
#include "common/sw_error.h"

/* -------------------------------------------------------------------------
 * 运动方向
 * ------------------------------------------------------------------------- */
typedef enum
{
    STEPPER_DIR_UP   = 0,   /* 顶刷上升 */
    STEPPER_DIR_DOWN = 1,   /* 顶刷下降 */
} drv_stepper_dir_t;

/* -------------------------------------------------------------------------
 * 接口声明
 * ------------------------------------------------------------------------- */

/**
 * @brief  初始化步进驱动器（ENA 有效，禁止脉冲输出）
 * @retval SW_OK
 */
sw_err_t drv_stepper_init(void);

/**
 * @brief  使能步进驱动器
 */
sw_err_t drv_stepper_enable(void);

/**
 * @brief  禁用步进驱动器（自由旋转模式）
 */
sw_err_t drv_stepper_disable(void);

/**
 * @brief  发送指定数量的脉冲（阻塞，脉冲期间不可中断）
 * @param  pulses   脉冲数量
 * @param  dir      运动方向
 * @param  pulse_us 单脉冲宽度（µs），建议 ≥ CFG_STEPPER_PULSE_US
 * @retval SW_OK / SW_ERR_PARAM
 * @note   此函数在 Linux 下使用 usleep 产生脉冲，精度受调度影响。
 *         如需精确定位，应在调用层做闭环（配合限位开关）。
 */
sw_err_t drv_stepper_move(uint32_t pulses, drv_stepper_dir_t dir, uint32_t pulse_us);

#endif /* DRV_STEPPER_H */
