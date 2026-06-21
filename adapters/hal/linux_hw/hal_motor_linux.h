/**
 * @file    hal_motor_linux.h
 * @brief   Linux 真机电机 HAL 内部接口（仅供机型适配层绑定实例）
 * @author  HUWANGWEI
 * @date    2026-06-15
 *
 * @note    仅供 adapters/machine/ 在 bootstrap 阶段调用；
 *          业务层仍通过 hal_motor_port 访问。
 */

#ifndef ADAPTERS_HAL_LINUX_HW_HAL_MOTOR_LINUX_H
#define ADAPTERS_HAL_LINUX_HW_HAL_MOTOR_LINUX_H

#ifdef __cplusplus
extern "C" {
#endif

#include "ports/hal/hal_motor_bind.h"
#include "common/sw_error.h"

/** @brief 注册 hal_motor_linux 实现到 hal_motor_port */
void hal_motor_linux_register(void);

/**
 * @brief  绑定指定 motor_id 的硬件配置并初始化驱动实例
 * @param  motor_id  与 domain motor id 一致
 * @param  cfg       绑定配置（VFD 串口指针须为静态存储）
 */
sw_err_t hal_motor_linux_bind(int motor_id, const hal_motor_bind_cfg_t *cfg);

#ifdef __cplusplus
}
#endif

#endif /* ADAPTERS_HAL_LINUX_HW_HAL_MOTOR_LINUX_H */
