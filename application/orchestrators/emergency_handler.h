/**
 * @file    emergency_handler.h
 * @brief   安全 LOCKOUT 与急停释放后归位协调
 * @author  HUWANGWEI
 * @date    2026-06-01
 *
 * @note    硬件急停停机由 safety_thread 负责（§7.3）；本模块处理
 *          LOCKOUT 洗车中止与 EVT_HW_ESTOP_OFF 后的安全归位。
 */

#ifndef APPLICATION_ORCHESTRATORS_EMERGENCY_HANDLER_H
#define APPLICATION_ORCHESTRATORS_EMERGENCY_HANDLER_H

#ifdef __cplusplus
extern "C" {
#endif

#include "common/sw_error.h"

/**
 * @brief  注册紧急动作事件订阅
 */
sw_err_t emergency_handler_init(void);

#ifdef __cplusplus
}
#endif

#endif /* APPLICATION_ORCHESTRATORS_EMERGENCY_HANDLER_H */
