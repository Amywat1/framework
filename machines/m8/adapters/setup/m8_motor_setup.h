/**
 * @file    m8_motor_setup.h
 * @brief   M8 机型电机 HAL 实例绑定
 * @author  HUWANGWEI
 * @date    2026-06-15
 */

#ifndef ADAPTERS_MACHINE_M8_M8_MOTOR_SETUP_H
#define ADAPTERS_MACHINE_M8_M8_MOTOR_SETUP_H

#ifdef __cplusplus
extern "C" {
#endif

#include "common/sw_error.h"

/**
 * @brief  按 M8 电机/VFD 配置表绑定 hal_motor 实例
 * @note   须在 wiring 注册 hal_motor 与 hal_vfd 之后、motor_init 之前调用
 */
sw_err_t m8_motor_setup(void);

#ifdef __cplusplus
}
#endif

#endif /* ADAPTERS_MACHINE_M8_M8_MOTOR_SETUP_H */
