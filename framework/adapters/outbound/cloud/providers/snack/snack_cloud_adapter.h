/**
 * @file    snack_cloud_adapter.h
 * @brief   Snack MQTT 云适配器接口（命令下行 + 状态上报）
 * @author  HUWANGWEI
 * @date    2026-04-10
 */

#ifndef FRAMEWORK_ADAPTERS_OUTBOUND_CLOUD_PROVIDERS_SNACK_CLOUD_ADAPTER_H
#define FRAMEWORK_ADAPTERS_OUTBOUND_CLOUD_PROVIDERS_SNACK_CLOUD_ADAPTER_H

#include "framework/ports/outbound/cloud/report/report_port.h"
#include "framework/common/sw_error.h"
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/** 命令分发器函数指针类型：处理一条下行 JSON 消息（可携带多个物模型属性）*/
typedef void (*snack_cloud_cmd_dispatch_fn_t)(const char *json);

/** 上报 JSON 构建器函数指针类型：cloud_report_payload_t -> JSON 字符串 */
typedef sw_err_t (*snack_cloud_report_builder_fn_t)(const cloud_report_payload_t *p,
                                                     char *buf, size_t buf_size);

/**
 * @brief  初始化 Snack MQTT 云连接并注册命令下行回调
 * @param  dispatch  命令分发函数，由调用方注入（机型特有的物模型点位分发）
 * @retval true   MQTT 连接成功
 * @retval false  连接失败，进入离线模式
 */
bool snack_cloud_command_adapter_init(snack_cloud_cmd_dispatch_fn_t dispatch);

/**
 * @brief  注册 Snack 云状态上报实现到 cloud_report_port
 * @param  builder  上报 JSON 构建函数，由调用方注入（机型特有的字段名与格式）
 * @note   须在 wiring 阶段调用，先于任何上报操作
 */
void snack_cloud_report_adapter_register(snack_cloud_report_builder_fn_t builder);

#ifdef __cplusplus
}
#endif

#endif /* FRAMEWORK_ADAPTERS_OUTBOUND_CLOUD_PROVIDERS_SNACK_CLOUD_ADAPTER_H */
