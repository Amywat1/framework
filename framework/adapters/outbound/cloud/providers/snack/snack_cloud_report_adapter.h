/**
 * @file    snack_cloud_report_adapter.h
 * @brief   Snack MQTT 云端状态上报出站适配器接口
 * @author  HUWANGWEI
 * @date    2026-04-10
 */

#ifndef FRAMEWORK_ADAPTERS_OUTBOUND_CLOUD_PROVIDERS_SNACK_CLOUD_REPORT_ADAPTER_H
#define FRAMEWORK_ADAPTERS_OUTBOUND_CLOUD_PROVIDERS_SNACK_CLOUD_REPORT_ADAPTER_H

#include "framework/common/sw_error.h"
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief  全量属性 JSON 构建器
 */
typedef sw_err_t (*cloud_report_build_fn_t)(char *buf, size_t buf_size);

/**
 * @brief  增量属性 JSON 构建器
 */
typedef sw_err_t (*cloud_report_build_delta_fn_t)(const char *const *ids,
                                                   size_t count,
                                                   char *buf,
                                                   size_t buf_size);

/**
 * @brief  注册 Snack 云状态上报实现到 cloud_report_port
 * @param  build_properties       全量属性 JSON 构建函数（项目注入）
 * @param  build_properties_delta 增量属性 JSON 构建函数；可为 NULL，回退为全量
 * @note   须在 wiring 阶段调用，先于任何上报操作
 */
void snack_cloud_report_adapter_register(cloud_report_build_fn_t build_properties,
                                         cloud_report_build_delta_fn_t build_properties_delta);

#ifdef __cplusplus
}
#endif

#endif /* FRAMEWORK_ADAPTERS_OUTBOUND_CLOUD_PROVIDERS_SNACK_CLOUD_REPORT_ADAPTER_H */
