/**
 * @file    safety_session_coordinator.h
 * @brief   安全会话协调（急停切断、CRITICAL 进表停运动与中止归位）
 *
 * @note    硬件急停热路径切断由急停采集通路调用 safety_cutout_execute；
 *          本模块在 dispatch 线程做 deferred_stop、abort_jobs 与 clear_machine。
 *          CRITICAL 新进表即停当前运动，不依赖 LOCKOUT 边沿；急停码除外。
 *          洗车中止后仍走清障；清障已被 CRITICAL/急停打断后由项目拒绝再开一轮。
 */

#ifndef APPLICATION_ORCHESTRATORS_SAFETY_SESSION_COORDINATOR_H
#define APPLICATION_ORCHESTRATORS_SAFETY_SESSION_COORDINATOR_H

#ifdef __cplusplus
extern "C" {
#endif

#include "common/sw_error.h"

/**
 * @brief  注册安全会话相关事件订阅
 */
sw_err_t safety_session_coordinator_init(void);

#ifdef __cplusplus
}
#endif

#endif /* APPLICATION_ORCHESTRATORS_SAFETY_SESSION_COORDINATOR_H */
