/**
 * @file    snack_cloud_report_adapter.h
 * @brief   Snack MQTT 云端状态上报出站适配器接口
 * @author  HUWANGWEI
 * @date    2026-04-10
 */

#ifndef FRAMEWORK_ADAPTERS_OUTBOUND_CLOUD_PROVIDERS_SNACK_CLOUD_REPORT_ADAPTER_H
#define FRAMEWORK_ADAPTERS_OUTBOUND_CLOUD_PROVIDERS_SNACK_CLOUD_REPORT_ADAPTER_H

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief  注册 Snack 云状态上报实现到 cloud_report_port
 * @note   须在 wiring 阶段调用；JSON 由 cloud_model_build_* 构建
 */
void snack_cloud_report_adapter_register(void);

#ifdef __cplusplus
}
#endif

#endif /* FRAMEWORK_ADAPTERS_OUTBOUND_CLOUD_PROVIDERS_SNACK_CLOUD_REPORT_ADAPTER_H */
