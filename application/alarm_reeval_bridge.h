/**
 * @file    alarm_reeval_bridge.h
 * @brief   ON_MOTION 报警重评估桥接（lifecycle event -> reeval group）
 * @author  HUWANGWEI
 * @date    2026-07-12
 */

#ifndef APPLICATION_ALARM_REEVAL_BRIDGE_H
#define APPLICATION_ALARM_REEVAL_BRIDGE_H

#ifdef __cplusplus
extern "C" {
#endif

#include "common/sw_error.h"
#include "domain/safety/model/alarm_types.h"

#include <stddef.h>

#define ALARM_REEVAL_BINDING_MAX 64U

/**
 * @brief  注册项目 ON_MOTION 重评估 binding 表并订阅 lifecycle 事件
 * @note   binding 表由项目静态持有；框架不复制，不定义项目分组枚举。
 */
sw_err_t alarm_reeval_bridge_init(const alarm_reeval_binding_t *bindings, size_t count);

/**
 * @brief  直接处理一个触发源，供项目 wiring 或单测复用
 */
sw_err_t alarm_reeval_bridge_handle(alarm_reeval_trigger_kind_t kind, uint16_t trigger_id);

#ifdef __cplusplus
}
#endif

#endif /* APPLICATION_ALARM_REEVAL_BRIDGE_H */
