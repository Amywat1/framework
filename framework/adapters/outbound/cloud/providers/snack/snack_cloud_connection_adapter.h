/**
 * @file    snack_cloud_connection_adapter.h
 * @brief   Snack MQTT 云端连接状态适配器接口
 * @author  HUWANGWEI
 * @date    2026-07-08
 */

#ifndef FRAMEWORK_ADAPTERS_OUTBOUND_CLOUD_PROVIDERS_SNACK_CLOUD_CONNECTION_ADAPTER_H
#define FRAMEWORK_ADAPTERS_OUTBOUND_CLOUD_PROVIDERS_SNACK_CLOUD_CONNECTION_ADAPTER_H

#include "framework/common/sw_error.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief  注册 Snack 云端连接 port 实现
 * @note   须在 wiring 阶段调用；不启动连接监测
 */
void snack_cloud_connection_adapter_register(void);

/**
 * @brief  启动连接边沿监测（MQTT 初始化完成后调用）
 * @note   注册周期任务检测 online/offline 变化，并作为 EVT_CLOUD_* 唯一生产者
 */
sw_err_t snack_cloud_connection_adapter_start(void);

#ifdef __cplusplus
}
#endif

#endif /* FRAMEWORK_ADAPTERS_OUTBOUND_CLOUD_PROVIDERS_SNACK_CLOUD_CONNECTION_ADAPTER_H */
