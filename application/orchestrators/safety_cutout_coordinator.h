/**
 * @file    safety_cutout_coordinator.h
 * @brief   安全切断协调（急停 / LOCKOUT 掐会话）
 * @author  HUWANGWEI
 * @date    2026-07-19
 *
 * @note    硬件急停热路径切断由急停采集通路调用 safety_cutout_execute 负责；
 *          本模块在 dispatch 线程做 deferred_stop 与 abort_wash。
 *          不执行中止归位（见 abort_home_coordinator）。
 */

#ifndef APPLICATION_ORCHESTRATORS_SAFETY_CUTOUT_COORDINATOR_H
#define APPLICATION_ORCHESTRATORS_SAFETY_CUTOUT_COORDINATOR_H

#ifdef __cplusplus
extern "C" {
#endif

#include "common/sw_error.h"

/**
 * @brief  注册安全切断相关事件订阅
 */
sw_err_t safety_cutout_coordinator_init(void);

#ifdef __cplusplus
}
#endif

#endif /* APPLICATION_ORCHESTRATORS_SAFETY_CUTOUT_COORDINATOR_H */
