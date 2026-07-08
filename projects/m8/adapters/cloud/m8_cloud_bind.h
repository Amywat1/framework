/**
 * @file    m8_cloud_bind.h
 * @brief   M8 云端传输绑定接口（对接 snack_cloud_*_adapter）
 * @author  HUWANGWEI
 * @date    2026-07-08
 */

#ifndef PROJECTS_M8_ADAPTERS_CLOUD_M8_CLOUD_BIND_H
#define PROJECTS_M8_ADAPTERS_CLOUD_M8_CLOUD_BIND_H

#include "framework/common/sw_error.h"
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief  构建全量属性上报 JSON
 */
sw_err_t m8_cloud_build_properties(char *buf, size_t buf_size);

/**
 * @brief  构建增量属性上报 JSON
 * @param  ids    物模型属性 id 数组
 * @param  count  id 数量
 */
sw_err_t m8_cloud_build_properties_delta(const char *const *ids,
                                          size_t count,
                                          char *buf,
                                          size_t buf_size);

/**
 * @brief  云端属性下发 JSON 分发入口
 * @note   匹配 snack_cloud_cmd_dispatch_fn_t
 */
void m8_cloud_on_property_set(const char *json_str);

#ifdef __cplusplus
}
#endif

#endif /* PROJECTS_M8_ADAPTERS_CLOUD_M8_CLOUD_BIND_H */
