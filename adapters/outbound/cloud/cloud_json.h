/**
 * @file    cloud_json.h
 * @brief   物模型的属性 JSON 门面与下行安装
 * @author  HUWANGWEI
 * @date    2026-08-04
 */

#ifndef ADAPTERS_OUTBOUND_CLOUD_CLOUD_JSON_H
#define ADAPTERS_OUTBOUND_CLOUD_CLOUD_JSON_H

#include "domain/cloud/cloud_point.h"

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @brief 属性下发处理完毕后的回复回调（由项目提供，可为 NULL 表示不回复） */
typedef sw_err_t (*cloud_property_reply_fn_t)(const char *request_json, const point_apply_result_t *result);

/**
 * @brief  安装属性下行：注册 COMMAND 提交回调，并把链路 recv 接到 JSON 分派
 * @param  submit COMMAND 提交回调，可为 NULL（无命令点位时）
 * @param  reply  处理结果回复回调，可为 NULL
 * @retval SW_OK  安装成功
 * @note   须在 `cloud_model_register()` 之后调用。链路未注册时仍可
 *         `cloud_json_apply_property_set()`，只是不绑定 recv。
 */
sw_err_t cloud_json_install(cloud_device_cmd_submit_fn_t submit, cloud_property_reply_fn_t reply);

/**
 * @brief  构建全量属性 JSON
 * @retval SW_OK           构建成功
 * @retval SW_ERR_NOT_INIT 物模型尚未注册
 */
sw_err_t cloud_json_build_properties(char *buf, size_t buf_size);

/**
 * @brief  构建指定 id 列表的增量属性 JSON
 * @retval SW_OK           构建成功
 * @retval SW_ERR_NOT_INIT 物模型尚未注册
 */
sw_err_t cloud_json_build_properties_delta(const char *const *ids, size_t count, char *buf, size_t buf_size);

/**
 * @brief  应用属性下发 JSON（供测试或入站适配器直连使用）
 * @param  result 应用结果输出，可为 NULL
 */
sw_err_t cloud_json_apply_property_set(const char *json_str, point_apply_result_t *result);

#ifdef __cplusplus
}
#endif

#endif /* ADAPTERS_OUTBOUND_CLOUD_CLOUD_JSON_H */
