/**
 * @file    alarm_binding_bridge.h
 * @brief   将报警入站端口绑定到 alarm_registry（消掉 domain 对 inbound 的依赖）
 * @author  HUWANGWEI
 * @date    2026-08-08
 */

#ifndef APPLICATION_BRIDGES_ALARM_BINDING_BRIDGE_H
#define APPLICATION_BRIDGES_ALARM_BINDING_BRIDGE_H

#ifdef __cplusplus
extern "C" {
#endif

#include "common/sw_error.h"

/**
 * @brief  注册 alarm_binding 入站端口，转调 alarm_registry 并在 LOCKOUT 时立即 drain
 * @retval SW_OK        注册成功
 * @retval SW_ERR_PARAM 注册被拒绝
 * @note   须在 alarm_registry_init() 之后、project_bind_alarm_catalog() 之前调用。
 * @note   trigger/clear 成功且姿态为 LOCKOUT 时，返回前调用 alarm_bridge_drain，
 *         使 EVT_SAFETY_LOCKOUT 入队；调用时 registry 锁已释放。
 */
sw_err_t alarm_binding_bridge_bind(void);

#ifdef __cplusplus
}
#endif

#endif /* APPLICATION_BRIDGES_ALARM_BINDING_BRIDGE_H */
