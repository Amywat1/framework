/**
 * @file    snack_cloud_command_adapter.h
 * @brief   Snack MQTT 云端属性入站适配器接口
 * @author  HUWANGWEI
 * @date    2026-04-10
 */

#ifndef FRAMEWORK_ADAPTERS_INBOUND_CLOUD_PROVIDERS_SNACK_CLOUD_COMMAND_ADAPTER_H
#define FRAMEWORK_ADAPTERS_INBOUND_CLOUD_PROVIDERS_SNACK_CLOUD_COMMAND_ADAPTER_H

#include "framework/common/point_table/point_table.h"
#include "framework/common/sw_error.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief  wiring 阶段注册 MQTT recv → cloud_property_port
 * @note   须在 snack_cloud_link_adapter_register() 之后调用
 */
sw_err_t snack_cloud_command_adapter_register(void);

/**
 * @brief  属性下发应答（供 cloud_model bundle.property_reply 注入）
 * @note   deploy 未配置 topicPropertyReply 时静默跳过
 */
sw_err_t snack_cloud_property_reply(const char *request_json,
                                     const point_apply_result_t *result);

#ifdef __cplusplus
}
#endif

#endif /* FRAMEWORK_ADAPTERS_INBOUND_CLOUD_PROVIDERS_SNACK_CLOUD_COMMAND_ADAPTER_H */
