/**
 * @file    alarm_bridge.h
 * @brief   报警域应用桥接（pending 排空、会话生命周期、ON_MOTION 重评估）
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
 * @brief  初始化报警桥接：注册 50ms drain 周期任务，并订阅洗车会话生命周期事件
 * @retval SW_OK 成功
 */
sw_err_t alarm_bridge_init(void);

/**
 * @brief  立即排空 registry pending（测试与同步场景可用）
 */
void alarm_bridge_drain(void);

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
