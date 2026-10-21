/**
 * @file    recovery_coordinator.h
 * @brief   Recover 用例协调：安全触发门禁、清非安全报警、等清故障、异步归位
 * @author  HUWANGWEI
 * @date    2026-07-09
 */

#ifndef APPLICATION_ORCHESTRATORS_RECOVERY_COORDINATOR_H
#define APPLICATION_ORCHESTRATORS_RECOVERY_COORDINATOR_H

#ifdef __cplusplus
extern "C" {
#endif

#include "common/sw_error.h"

/**
 * @brief  初始化恢复协调器并订阅相关事件
 * @retval SW_OK        订阅成功
 * @retval SW_ERR_*     event_subscribe_table 失败
 *
 * @note   起步：安全触发还在则失败。再调机型 clear_alarms，等 poll_clear_alarms
 *         （未注册则不等），然后 reset_non_safety，再归位。
 *         归位成功后 reset_all 摘掉条件已消失的锁存，当时无禁止接单报警则待机，否则停机。
 *         离开 RECOVERING 取消等待，清故障等待用 CAS 认领，迟到完成忽略。
 *         开归位再认领 IDLE→WAIT_HOME，认领后已离开则退回 IDLE。
 */
sw_err_t recovery_coordinator_init(void);

/**
 * @brief  推进异步清故障等待
 * @note   无等待或未认领到 WAIT_CLEAR 时立即返回。事件线程与周期任务可并发调用。
 */
void recovery_coordinator_poll(void);

#ifdef __cplusplus
}
#endif

#endif /* APPLICATION_ORCHESTRATORS_RECOVERY_COORDINATOR_H */
