/**
 * @file    alarm_bridge.h
 * @brief   报警应用桥接：入站端口绑定、会话 journal、可选 ON_MOTION 重评估
 * @author  HUWANGWEI
 * @date    2026-08-08
 */

#ifndef APPLICATION_BRIDGES_ALARM_BRIDGE_H
#define APPLICATION_BRIDGES_ALARM_BRIDGE_H

#ifdef __cplusplus
extern "C" {
#endif

#include "common/sw_error.h"
#include "domain/safety/model/alarm_types.h"

#include <stddef.h>

/** @brief ON_MOTION 重评估 binding 表容量上限 */
#define ALARM_REEVAL_BINDING_MAX 64U

/**
 * @brief  注册 alarm_binding 入站端口，转调 alarm_registry
 * @retval SW_OK        注册成功
 * @retval SW_ERR_PARAM 注册被拒绝
 * @note   须在 alarm_registry_init() 之后、project_bind_alarm_catalog() 之前调用。
 */
sw_err_t alarm_bridge_bind(void);

/**
 * @brief  初始化报警桥接：订阅洗车会话生命周期事件
 * @retval SW_OK 成功
 */
sw_err_t alarm_bridge_init(void);

/**
 * @brief  注册项目 ON_MOTION 重评估 binding 表并订阅相关事件
 * @note   binding 表由项目静态持有；框架不复制。由项目在 init_adapters 中按需调用，
 *         bootstrap 不代替项目做此选择。
 */
sw_err_t alarm_bridge_reeval_init(const alarm_reeval_binding_t *bindings, size_t count);

/**
 * @brief  直接处理一个重评估触发源，供项目 wiring 或单测复用
 */
sw_err_t alarm_bridge_reeval_handle(alarm_reeval_trigger_kind_t kind, uint16_t trigger_id);

#ifdef __cplusplus
}
#endif

#endif /* APPLICATION_BRIDGES_ALARM_BRIDGE_H */
