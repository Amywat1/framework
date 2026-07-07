/**
 * @file    snack_cloud_report_adapter.h
 * @brief   Snack MQTT 云端状态上报出站适配器接口
 * @author  HUWANGWEI
 * @date    2026-04-10
 */

#ifndef FRAMEWORK_ADAPTERS_OUTBOUND_CLOUD_PROVIDERS_SNACK_CLOUD_REPORT_ADAPTER_H
#define FRAMEWORK_ADAPTERS_OUTBOUND_CLOUD_PROVIDERS_SNACK_CLOUD_REPORT_ADAPTER_H

#include "framework/ports/outbound/cloud/report/report_port.h"
#include "framework/common/sw_error.h"
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/** 上报 JSON 构建器函数指针类型：cloud_report_payload_t -> JSON 字符串 */
typedef sw_err_t (*snack_cloud_report_builder_fn_t)(const cloud_report_payload_t *p,
                                                     char *buf, size_t buf_size);

/**
 * @brief  注册 Snack 云状态上报实现到 cloud_report_port
 * @param  builder  上报 JSON 构建函数，由调用方注入（机型特有的字段名与格式）
 * @note   须在 wiring 阶段调用，先于任何上报操作
 */
void snack_cloud_report_adapter_register(snack_cloud_report_builder_fn_t builder);

#ifdef __cplusplus
}
#endif

#endif /* FRAMEWORK_ADAPTERS_OUTBOUND_CLOUD_PROVIDERS_SNACK_CLOUD_REPORT_ADAPTER_H */
