/**
 * @file    cloud_model.h
 * @brief   项目物模型一次注册入口
 * @author  HUWANGWEI
 * @date    2026-07-08
 */

#ifndef CLOUD_CLOUD_MODEL_H
#define CLOUD_CLOUD_MODEL_H

#ifdef __cplusplus
extern "C" {
#endif

#include "application/orchestrators/report_scheduler.h"
#include "cloud/cloud_point.h"
#include "common/point_table/point_table.h"
#include "common/sw_error.h"

#include <stddef.h>

typedef sw_err_t (*cloud_property_reply_fn_t)(const char *request_json, const point_apply_result_t *result);

/**
 * @brief  项目物模型注册包（wiring 阶段一次传入）
 */
typedef struct {
    const cloud_point_entry_t   *entries;
    size_t                       count;
    const report_policy_entry_t *report_policies;
    size_t                       policy_count;
    cloud_property_reply_fn_t    property_reply;
} cloud_model_bundle_t;

/**
 * @brief  注册物模型并安装 property_port（不含 MQTT init）
 */
sw_err_t cloud_model_register(const cloud_model_bundle_t *bundle);

/**
 * @brief  校验点位表并初始化 ON_CHANGE watcher
 */
sw_err_t cloud_model_validate_and_watch(void);

/**
 * @brief  启动上报调度器（bootstrap application 阶段）
 */
sw_err_t cloud_model_start_scheduler(void);

/**
 * @brief  全量/增量 JSON 构建（report adapter 使用）
 */
sw_err_t cloud_model_build_properties(char *buf, size_t buf_size);
sw_err_t cloud_model_build_properties_delta(const char *const *ids, size_t count, char *buf, size_t buf_size);

/**
 * @brief  测试直连入口
 */
sw_err_t cloud_model_apply_property_set(const char *json_str, point_apply_result_t *result);

#ifdef __cplusplus
}
#endif

#endif /* CLOUD_CLOUD_MODEL_H */
