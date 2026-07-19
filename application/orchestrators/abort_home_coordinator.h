/**
 * @file    abort_home_coordinator.h
 * @brief   中止归位协调（非急停洗车中止后的清障）
 * @author  HUWANGWEI
 * @date    2026-07-19
 *
 * @note    仅响应 EVT_ABORT_HOME_REQUESTED；模式进入 ABORT_HOMING 由 operational_mode 决定。
 */

#ifndef APPLICATION_ORCHESTRATORS_ABORT_HOME_COORDINATOR_H
#define APPLICATION_ORCHESTRATORS_ABORT_HOME_COORDINATOR_H

#ifdef __cplusplus
extern "C" {
#endif

#include "common/sw_error.h"

/**
 * @brief  注册中止归位事件订阅
 */
sw_err_t abort_home_coordinator_init(void);

#ifdef __cplusplus
}
#endif

#endif /* APPLICATION_ORCHESTRATORS_ABORT_HOME_COORDINATOR_H */
