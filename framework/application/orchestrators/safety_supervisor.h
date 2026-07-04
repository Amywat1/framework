/**
 * @file    safety_supervisor.h
 * @brief   安全监督器：订阅安全/报警事件，投影到 dev_ctx
 * @author  HUWANGWEI
 * @date    2026-06-26
 *
 * @note    domain/safety 只发布事件、不写状态快照（分层约束）。safety_supervisor
 *          作为应用层订阅者，把安全态与活跃报警投影到 service/dev_ctx，供云上报、
 *          CLI 查询读取。本期不触发机型复位动作（AUTO_STATIC 报警自动清除），
 *          复位/failsafe 动作端口留作后续扩展。
 */

#ifndef APPLICATION_ORCHESTRATORS_SAFETY_SUPERVISOR_H
#define APPLICATION_ORCHESTRATORS_SAFETY_SUPERVISOR_H

#ifdef __cplusplus
extern "C" {
#endif

#include "framework/common/sw_error.h"

/**
 * @brief  初始化安全监督器并订阅安全/报警事件
 * @retval SW_OK / SW_ERR_NOT_INIT / SW_ERR_OVERFLOW
 */
sw_err_t safety_supervisor_init(void);

#ifdef __cplusplus
}
#endif

#endif /* APPLICATION_ORCHESTRATORS_SAFETY_SUPERVISOR_H */
