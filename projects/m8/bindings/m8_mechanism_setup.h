/**
 * @file    m8_mechanism_setup.h
 * @brief   M8 命名机构装配接口
 * @author  HUWANGWEI
 * @date    2026-07-11
 *
 * @note    须在 m8_motor_exec_init() 完成后、m8_motor_exec_start() 之前调用。
 */

#ifndef M8_BINDINGS_M8_MECHANISM_SETUP_H
#define M8_BINDINGS_M8_MECHANISM_SETUP_H

#include "framework/common/sw_error.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @brief 可按位组合的机构模块掩码（用于测试或裁剪 init）。 */
typedef enum
{
    M8_DOMAIN_GANTRY    = (1U << 0), /**< 龙门行走 */
    M8_DOMAIN_BRUSH     = (1U << 1), /**< 侧刷/顶刷 */
    M8_DOMAIN_LIFT      = (1U << 2), /**< 顶刷升降 */
    M8_DOMAIN_REAR_LOCK = (1U << 3), /**< 后轮锁止 */
    M8_DOMAIN_FAN       = (1U << 4), /**< 风机 */
    M8_DOMAIN_ALL       = (M8_DOMAIN_GANTRY | M8_DOMAIN_BRUSH | M8_DOMAIN_LIFT
                         | M8_DOMAIN_REAR_LOCK | M8_DOMAIN_FAN),
} m8_mechanism_mask_t;

/**
 * @brief 按掩码初始化指定 M8 命名机构。
 *
 * @param mask  机构掩码；0 表示 M8_DOMAIN_ALL。
 * @return SW_OK 成功；SW_ERR_NOT_INIT 执行器未初始化；其余为各机构 init 返回值。
 */
sw_err_t m8_mechanism_setup_mask(uint32_t mask);

/** @brief 初始化全部 M8 命名机构。 */
sw_err_t m8_mechanism_setup(void);

#ifdef __cplusplus
}
#endif

#endif /* M8_BINDINGS_M8_MECHANISM_SETUP_H */
