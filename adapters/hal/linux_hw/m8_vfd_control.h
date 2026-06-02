/**
 * @file    m8_vfd_control.h
 * @brief   M8 VFD 统一控制（hal_motion / hal_motor 共用）
 * @author  胡望伟
 * @date    2026-06-01
 */

#ifndef ADAPTERS_HAL_LINUX_HW_M8_VFD_CONTROL_H
#define ADAPTERS_HAL_LINUX_HW_M8_VFD_CONTROL_H

#ifdef __cplusplus
extern "C" {
#endif

#include "common/sw_error.h"
#include <stdbool.h>
#include <stdint.h>

/**
 * @brief  龙门 VFD 设速（speed_ref 单位 0.01Hz，正转>0，反转<0，0=停止）
 */
sw_err_t m8_vfd_gantry_set_speed(int speed_ref);

/**
 * @brief  刷子 VFD 设速（仅支持正转>0 与停止）
 */
sw_err_t m8_vfd_brush_set_speed(int speed_ref);

/** @brief 龙门 VFD 故障复位 */
sw_err_t m8_vfd_gantry_fault_reset(void);

/** @brief 刷子 VFD 故障复位 */
sw_err_t m8_vfd_brush_fault_reset(void);

/** @brief 刷子 VFD 是否处于正转运行态 */
bool m8_vfd_brush_is_running(void);

#ifdef __cplusplus
}
#endif

#endif /* ADAPTERS_HAL_LINUX_HW_M8_VFD_CONTROL_H */
