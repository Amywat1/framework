/**
 * @file    snack_cloud_command_adapter.h
 * @brief   Snack MQTT 云端命令入站适配器接口
 * @author  HUWANGWEI
 * @date    2026-04-10
 */

#ifndef FRAMEWORK_ADAPTERS_INBOUND_CLOUD_PROVIDERS_SNACK_CLOUD_COMMAND_ADAPTER_H
#define FRAMEWORK_ADAPTERS_INBOUND_CLOUD_PROVIDERS_SNACK_CLOUD_COMMAND_ADAPTER_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/** 命令分发器函数指针类型：处理一条下行 JSON 消息（可携带多个物模型属性）*/
typedef void (*snack_cloud_cmd_dispatch_fn_t)(const char *json);

/**
 * @brief  初始化 Snack MQTT 云连接并注册命令下行回调
 * @param  dispatch  命令分发函数，由调用方注入（机型特有的物模型点位分发）
 * @retval true   MQTT 连接成功
 * @retval false  连接失败，进入离线模式
 */
bool snack_cloud_command_adapter_init(snack_cloud_cmd_dispatch_fn_t dispatch);

#ifdef __cplusplus
}
#endif

#endif /* FRAMEWORK_ADAPTERS_INBOUND_CLOUD_PROVIDERS_SNACK_CLOUD_COMMAND_ADAPTER_H */
