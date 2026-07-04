/**
 * @file    emergency_handler.h
 * @brief   安全 LOCKOUT 统一动作处理
 * @author  HUWANGWEI
 * @date    2026-06-01
 *
 * @note    LOCKOUT 时调用 wash_orchestrator_abort() 中止洗车并停止输出。
 */

#ifndef APPLICATION_ORCHESTRATORS_EMERGENCY_HANDLER_H
#define APPLICATION_ORCHESTRATORS_EMERGENCY_HANDLER_H

#ifdef __cplusplus
extern "C" {
#endif

#include "framework/common/sw_error.h"

/**
 * @brief  注册 LOCKOUT 紧急动作订阅
 * @retval SW_OK / SW_ERR_HW
 */
sw_err_t emergency_handler_init(void);

#ifdef __cplusplus
}
#endif

#endif /* APPLICATION_ORCHESTRATORS_EMERGENCY_HANDLER_H */
