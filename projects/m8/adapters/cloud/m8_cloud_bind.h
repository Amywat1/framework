/**
 * @file    m8_cloud_bind.h
 * @brief   M8 云端传输绑定接口（对接 snack_cloud_*_adapter）
 * @author  HUWANGWEI
 * @date    2026-07-08
 */

#ifndef PROJECTS_M8_ADAPTERS_CLOUD_M8_CLOUD_BIND_H
#define PROJECTS_M8_ADAPTERS_CLOUD_M8_CLOUD_BIND_H

#include "framework/ports/outbound/cloud/report/report_port.h"
#include "framework/common/sw_error.h"
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief  云端属性上报 JSON 构建回调
 * @note   匹配 snack_cloud_report_builder_fn_t
 */
sw_err_t m8_cloud_on_report(const cloud_report_payload_t *p,
                             char *buf, size_t buf_size);

/**
 * @brief  云端属性下发 JSON 分发入口
 * @note   匹配 snack_cloud_cmd_dispatch_fn_t
 */
void m8_cloud_on_property_set(const char *json_str);

#ifdef __cplusplus
}
#endif

#endif /* PROJECTS_M8_ADAPTERS_CLOUD_M8_CLOUD_BIND_H */
