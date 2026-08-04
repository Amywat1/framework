/**
 * @file    snack_cloud_command_adapter.h
 * @brief   Snack MQTT 云端属性入站适配器接口
 * @author  HUWANGWEI
 * @date    2026-04-10
 */

#ifndef ADAPTERS_INBOUND_CLOUD_PROVIDERS_SNACK_SNACK_CLOUD_COMMAND_ADAPTER_H
#define ADAPTERS_INBOUND_CLOUD_PROVIDERS_SNACK_SNACK_CLOUD_COMMAND_ADAPTER_H

#include "common/point_table/point_table.h"
#include "common/sw_error.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief  注册 Snack 云命令适配器。
 *
 * @note   本函数不绑定 MQTT 接收回调；回调绑定由 snack_cloud_command_adapter_bind() 完成。
 */
sw_err_t snack_cloud_command_adapter_register(void);

/**
 * @brief  配置属性下发应答 topic。
 *
 * @param  topic_property_reply 属性下发应答 topic；传入 NULL 或空字符串表示禁用应答。
 * @retval SW_OK 配置成功。
 * @retval SW_ERR_PARAM topic 长度超过内部缓冲区。
 * @note   须在 snack_cloud_property_reply() 前调用；未配置时应答静默跳过。
 */
sw_err_t snack_cloud_command_adapter_configure(const char *topic_property_reply);

/**
 * @brief  绑定 MQTT 接收回调到 cloud_property_port 分发逻辑。
 *
 * @retval SW_OK 绑定成功。
 * @retval SW_ERR_NOT_INIT cloud_link_port 未注册或不支持设置接收回调。
 * @note   须在 cloud_link_port 注册后调用；本函数不读取部署配置。
 */
sw_err_t snack_cloud_command_adapter_bind(void);

/**
 * @brief  属性下发应答（供 cloud_model bundle.property_reply 注入）
 * @note   未配置 topicPropertyReply 时静默跳过。
 */
sw_err_t snack_cloud_property_reply(const char *request_json, const point_apply_result_t *result);

#ifdef __cplusplus
}
#endif

#endif /* ADAPTERS_INBOUND_CLOUD_PROVIDERS_SNACK_SNACK_CLOUD_COMMAND_ADAPTER_H */
