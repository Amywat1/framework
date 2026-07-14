/**
 * @file    snack_cloud_report_adapter.h
 * @brief   Snack MQTT 云端状态上报出站适配器接口
 * @author  HUWANGWEI
 * @date    2026-04-10
 */

#ifndef FRAMEWORK_ADAPTERS_OUTBOUND_CLOUD_PROVIDERS_SNACK_CLOUD_REPORT_ADAPTER_H
#define FRAMEWORK_ADAPTERS_OUTBOUND_CLOUD_PROVIDERS_SNACK_CLOUD_REPORT_ADAPTER_H

#include "common/sw_error.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief  注册 Snack 云状态上报实现到 cloud_report_port
 * @note   本函数只注册 ops；JSON 由 cloud_model_build_* 构建。
 */
void snack_cloud_report_adapter_register(void);

/**
 * @brief  配置属性上报 topic。
 *
 * @param  topic_property_up 属性上报 topic，必须为非空字符串。
 * @retval SW_OK 配置成功。
 * @retval SW_ERR_PARAM 参数为空或长度超过内部缓冲区。
 * @note   须在 publish_properties() 或 publish_properties_delta() 前调用。
 */
sw_err_t snack_cloud_report_adapter_configure(const char *topic_property_up);

#ifdef __cplusplus
}
#endif

#endif /* FRAMEWORK_ADAPTERS_OUTBOUND_CLOUD_PROVIDERS_SNACK_CLOUD_REPORT_ADAPTER_H */
