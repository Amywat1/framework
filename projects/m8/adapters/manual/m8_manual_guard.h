/**
 * @file    m8_manual_guard.h
 * @brief   M8 手动机构动作准入守卫
 * @author  HUWANGWEI
 * @date    2026-07-08
 *
 * @note    仅依据 device_state 与急停 DI 判定，不使用 safety_state。
 *          允许点动：DEV_STATE_IDLE / DEV_STATE_STOP / DEV_STATE_FAULT。
 */

#ifndef PROJECTS_M8_ADAPTERS_MANUAL_M8_MANUAL_GUARD_H
#define PROJECTS_M8_ADAPTERS_MANUAL_M8_MANUAL_GUARD_H

#include "framework/common/sw_error.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief  手动启动/运动类动作是否允许
 * @retval SW_OK         允许
 * @retval SW_ERR_STATE  设备态不允许或急停有效
 */
sw_err_t m8_manual_guard_allow_motion(void);

/**
 * @brief  手动停止类动作是否允许
 * @retval SW_OK  允许（不限制 device_state）
 */
sw_err_t m8_manual_guard_allow_stop(void);

#ifdef __cplusplus
}
#endif

#endif /* PROJECTS_M8_ADAPTERS_MANUAL_M8_MANUAL_GUARD_H */
