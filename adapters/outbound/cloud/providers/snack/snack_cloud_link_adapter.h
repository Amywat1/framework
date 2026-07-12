/**
 * @file    snack_cloud_link_adapter.h
 * @brief   Snack MQTT 云端链路适配器接口
 * @author  HUWANGWEI
 * @date    2026-07-08
 */

#ifndef FRAMEWORK_ADAPTERS_OUTBOUND_CLOUD_PROVIDERS_SNACK_CLOUD_LINK_ADAPTER_H
#define FRAMEWORK_ADAPTERS_OUTBOUND_CLOUD_PROVIDERS_SNACK_CLOUD_LINK_ADAPTER_H

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief  注册 Snack 实现到 cloud_link_port（wiring 阶段调用）
 */
void snack_cloud_link_adapter_register(void);

#ifdef __cplusplus
}
#endif

#endif /* FRAMEWORK_ADAPTERS_OUTBOUND_CLOUD_PROVIDERS_SNACK_CLOUD_LINK_ADAPTER_H */
