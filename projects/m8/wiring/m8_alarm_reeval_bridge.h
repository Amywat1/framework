/**
 * @file    m8_alarm_reeval_bridge.h
 * @brief   M8 机构/流程生命周期事件 → ON_MOTION 重评估桥接
 * @author  HUWANGWEI
 * @date    2026-07-10
 */

#ifndef M8_ALARM_REEVAL_BRIDGE_H
#define M8_ALARM_REEVAL_BRIDGE_H

#ifdef __cplusplus
extern "C" {
#endif

#include "framework/common/sw_error.h"

/**
 * @brief  订阅 lifecycle 事件并注册 ON_MOTION 重评估桥接
 * @note   须在 event_bus_init() 与 alarm_registry_load_catalog() 之后、poll 启动前调用
 */
sw_err_t m8_alarm_reeval_bridge_init(void);

#ifdef __cplusplus
}
#endif

#endif /* M8_ALARM_REEVAL_BRIDGE_H */
