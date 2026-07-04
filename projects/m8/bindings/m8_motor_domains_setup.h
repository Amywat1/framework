/**
 * @file    m8_motor_domains_setup.h
 * @brief   M8 机型电机相关领域模块装配接口。
 *
 * 将 m8_motor_exec_init() 建立的共享执行器注入各 mechanism 领域层。
 * 硬件端口与 MCC 配置由 m8_motor_exec 负责。
 *
 * @note 须在 m8_motor_exec_init() 完成后、m8_motor_exec_start() 之前调用。
 */
#ifndef MACHINES_M8_ADAPTERS_SETUP_M8_MOTOR_DOMAINS_SETUP_H
#define MACHINES_M8_ADAPTERS_SETUP_M8_MOTOR_DOMAINS_SETUP_H

#include "framework/common/sw_error.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @brief 可按位组合的电机领域模块掩码（用于测试或裁剪 init）。 */
typedef enum {
    M8_DOMAIN_GANTRY    = (1U << 0), /**< 龙门行走 */
    M8_DOMAIN_BRUSH     = (1U << 1), /**< 侧刷/顶刷 */
    M8_DOMAIN_LIFT      = (1U << 2), /**< 顶刷升降 */
    M8_DOMAIN_REAR_LOCK = (1U << 3), /**< 后轮锁止 */
    M8_DOMAIN_FAN       = (1U << 4), /**< 风机 */
    M8_DOMAIN_ALL       = (M8_DOMAIN_GANTRY | M8_DOMAIN_BRUSH | M8_DOMAIN_LIFT
                         | M8_DOMAIN_REAR_LOCK | M8_DOMAIN_FAN),
} m8_motor_domain_mask_t;

/**
 * @brief 按掩码将共享执行器注入指定电机领域模块。
 *
 * @param mask  领域掩码；0 表示 M8_DOMAIN_ALL。
 * @return SW_OK 成功；SW_ERR_NOT_INIT 执行器未初始化；其余为各 domain_init 返回值。
 */
sw_err_t m8_motor_domains_setup_mask(uint32_t mask);

/**
 * @brief 将全部电机领域模块绑定到共享 motor_executor_t。
 */
sw_err_t m8_motor_domains_setup(void);

#ifdef __cplusplus
}
#endif

#endif /* MACHINES_M8_ADAPTERS_SETUP_M8_MOTOR_DOMAINS_SETUP_H */
