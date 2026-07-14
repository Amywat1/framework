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
 * @brief  注册物模型并安装 property_port。
 *
 * @param  bundle 项目物模型注册包，entries 和 count 必须有效。
 * @retval SW_OK 注册成功。
 * @retval SW_ERR_PARAM 参数为空或点位表为空。
 * @note   本函数不校验 getter，不初始化 watcher，也不启动运行态。
 */
sw_err_t cloud_model_register(const cloud_model_bundle_t *bundle);

/**
 * @brief  校验物模型点位表。
 *
 * @retval SW_OK 校验成功。
 * @retval SW_ERR_NOT_INIT 物模型尚未注册。
 * @retval 其他 点位表校验失败。
 * @note   本函数只执行静态校验，不调用 getter，不初始化 watcher，不写 shadow。
 */
sw_err_t cloud_model_validate(void);

/**
 * @brief  初始化物模型运行期 watcher。
 *
 * @retval SW_OK 初始化成功。
 * @retval SW_ERR_NOT_INIT 物模型尚未注册。
 * @retval 其他 watcher 初始化失败。
 * @note   本函数会为 ON_CHANGE 点位建立 shadow，应在 validate 阶段之后、运行态启动前调用。
 */
sw_err_t cloud_model_init(void);

/**
 * @brief  注册上报调度策略。
 *
 * @retval SW_OK 注册成功。
 * @retval SW_ERR_NOT_INIT 未配置上报策略。
 * @retval 其他 调度任务或事件订阅注册失败。
 * @note   本函数只注册周期任务和事件订阅，实际执行由 scheduler start 阶段统一启动。
 */
sw_err_t cloud_model_register_scheduler(void);

/**
 * @brief  构建全量属性 JSON。
 *
 * @param  buf 输出缓冲区。
 * @param  buf_size 输出缓冲区大小。
 * @retval SW_OK 构建成功。
 * @retval SW_ERR_NOT_INIT 物模型尚未注册。
 * @retval 其他 JSON 构建失败。
 */
sw_err_t cloud_model_build_properties(char *buf, size_t buf_size);

/**
 * @brief  构建增量属性 JSON。
 *
 * @param  ids 需要输出的属性 id 列表。
 * @param  count 属性 id 数量。
 * @param  buf 输出缓冲区。
 * @param  buf_size 输出缓冲区大小。
 * @retval SW_OK 构建成功。
 * @retval SW_ERR_NOT_INIT 物模型尚未注册。
 * @retval 其他 JSON 构建失败。
 */
sw_err_t cloud_model_build_properties_delta(const char *const *ids, size_t count, char *buf, size_t buf_size);

/**
 * @brief  应用属性下发 JSON。
 *
 * @param  json_str 属性下发 JSON 字符串。
 * @param  result 应用结果输出，可为 NULL。
 * @retval SW_OK 应用成功。
 * @retval 其他 解析或写入失败。
 * @note   主要供测试或入站适配器直连使用。
 */
sw_err_t cloud_model_apply_property_set(const char *json_str, point_apply_result_t *result);

#ifdef __cplusplus
}
#endif

#endif /* CLOUD_CLOUD_MODEL_H */
