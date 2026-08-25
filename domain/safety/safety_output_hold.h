/**
 * @file    safety_output_hold.h
 * @brief   机构输出抑制：急停 DI 电平或软件锁存，热路径可读、无锁。
 *
 * @note    能量切断仍走 `safety_cutout_execute()`。本模块只回答「本拍还能否
 *          写电机/阀泵」：电机 tick 与流体 poll 读同一电平，不再各持私有旗标。
 *          采样到 DI 有效会锁存，按钮松开后仍保持抑制，直到
 *          `safety_output_hold_release()`。这是复位急停抑制的唯一入口。
 */
#ifndef DOMAIN_SAFETY_SAFETY_OUTPUT_HOLD_H
#define DOMAIN_SAFETY_SAFETY_OUTPUT_HOLD_H

#ifdef __cplusplus
extern "C" {
#endif

#include "common/sw_error.h"

#include <stdbool.h>

/**
 * @brief  绑定急停 DI 读取函数
 * @param  is_active  返回急停输入是否有效；NULL 表示无 DI（视为无效）
 * @note   由 `safety_port_register()` 在注册成功时绑定 `estop_is_active`；
 *         解除注册时传 NULL。单元测试可直接注入。
 */
void safety_output_hold_bind_di(bool (*is_active)(void));

/**
 * @brief  置位软件锁存（急停热路径可用，无锁）
 * @note   硬件急停边沿应在切断能量后调用，避免短脉冲在下一拍采样前丢失。
 */
void safety_output_hold_request(void);

/**
 * @brief  输出抑制是否有效
 * @retval true  急停 DI 有效，或软件锁存已置位（采样到 DI 有效时会锁存）
 * @retval false 无 DI 且锁存已释放
 */
bool safety_output_hold_is_active(void);

/**
 * @brief  释放软件锁存
 * @retval SW_OK        已释放，或本来就未锁存
 * @retval SW_ERR_STATE 急停 DI 仍有效，拒绝释放
 */
sw_err_t safety_output_hold_release(void);

/**
 * @brief  清空锁存并解除 DI 绑定
 * @note   供单元测试与 `port_registry_safety_reset()` 使用，生产路径不要调用。
 */
void safety_output_hold_reset(void);

#ifdef __cplusplus
}
#endif

#endif /* DOMAIN_SAFETY_SAFETY_OUTPUT_HOLD_H */
