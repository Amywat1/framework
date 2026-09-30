/**
 * @file    cloud_json.h
 * @brief   物模型的属性 JSON 门面与下行安装
 * @author  HUWANGWEI
 * @date    2026-08-04
 */

#ifndef ADAPTERS_OUTBOUND_CLOUD_CLOUD_JSON_H
#define ADAPTERS_OUTBOUND_CLOUD_CLOUD_JSON_H

#include "domain/cloud/cloud_point.h"

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @brief 属性下发处理完毕后的回复回调（由项目提供，可为 NULL 表示不回复） */
typedef sw_err_t (*cloud_property_reply_fn_t)(const char *request_json, const point_apply_result_t *result);

/**
 * @brief  属性上行允许函数
 * @param  id 点位标识，不为 NULL
 * @return true 表示该点可进入属性 JSON
 */
typedef bool (*cloud_report_allow_fn_t)(const char *id);

/**
 * @brief  安装属性下行：注册 COMMAND 提交回调，并把链路 recv 接到 JSON 分派
 * @param  submit COMMAND 提交回调，可为 NULL（无命令点位时）
 * @param  reply  处理结果回复回调，可为 NULL
 * @retval SW_OK  安装成功
 * @note   须在 `cloud_model_register()` 之后调用。链路未注册时仍可
 *         `cloud_json_apply_property_set()`，只是不绑定 recv。
 */
sw_err_t cloud_json_install(cloud_device_cmd_submit_fn_t submit, cloud_property_reply_fn_t reply);

/** 脉冲点回显 1 后，再报空闲 0 的保持时间（ms） */
#define CLOUD_PULSE_ECHO_HOLD_MS 500U

/**
 * @brief  到期则上报已推迟的脉冲空闲 0。
 * @note   由链路 poll 调用；接收回调内不得阻塞等待。
 */
void cloud_json_poll(void);

/**
 * @brief  清空推迟的空闲上报（仅供单元测试）
 */
void cloud_json_reset_for_test(void);

/**
 * @brief  设置属性上行允许函数
 * @param  fn 返回 true 表示该点可进入属性 JSON；NULL 表示不过滤
 * @note   仅作用于 `cloud_json_build_properties()` 与
 *         `cloud_json_build_properties_delta()`。下行回显与
 *         `cloud_json_build_properties_all()` 不受影响。
 */
void cloud_json_set_report_allow(cloud_report_allow_fn_t fn);

/**
 * @brief  构建属性快照 JSON（受允许函数过滤）
 * @retval SW_OK           构建成功；无合格点时写入 `{}`
 * @retval SW_ERR_NOT_INIT 物模型尚未注册
 */
sw_err_t cloud_json_build_properties(char *buf, size_t buf_size);

/**
 * @brief  构建全量属性 JSON，忽略上报白名单
 * @retval SW_OK           构建成功
 * @retval SW_ERR_NOT_INIT 物模型尚未注册
 */
sw_err_t cloud_json_build_properties_all(char *buf, size_t buf_size);

/**
 * @brief  构建指定 id 列表的增量属性 JSON（受允许函数过滤）
 * @retval SW_OK           构建成功；过滤后为空时写入 `{}`
 * @retval SW_ERR_NOT_INIT 物模型尚未注册
 */
sw_err_t cloud_json_build_properties_delta(const char *const *ids, size_t count, char *buf, size_t buf_size);

/**
 * @brief  属性分包回调
 * @param  json 单包合法对象，长度小于 CLOUD_REPORT_JSON_MAX
 * @param  ctx  调用方上下文
 * @return 回调返回值；非 SW_OK 时停止后续分包
 */
typedef sw_err_t (*cloud_json_chunk_fn_t)(const char *json, void *ctx);

/**
 * @brief  按单包上限把指定 id 拆成多段合法 JSON 并逐包回调
 * @param  ids   点位标识列表
 * @param  count 列表长度
 * @param  fn    每包回调，不得为 NULL
 * @param  ctx   透传给回调
 * @retval SW_OK        全部处理完（被跳过的超长单点视为已处理）
 * @retval SW_ERR_PARAM 参数无效
 * @note   按点位贪心装箱，每包都是完整 JSON 对象。单点本身超过上限则跳过并告警。
 */
sw_err_t cloud_json_visit_ids(const char *const *ids, size_t count, cloud_json_chunk_fn_t fn, void *ctx);

/**
 * @brief  按单包上限遍历快照
 * @param  apply_filter true 走白名单；false 忽略白名单
 * @param  fn           每包回调，不得为 NULL
 * @param  ctx          透传给回调
 * @retval SW_OK           遍历完成；无合格点时直接成功
 * @retval SW_ERR_NOT_INIT 物模型尚未注册
 */
sw_err_t cloud_json_visit_snapshot(bool apply_filter, cloud_json_chunk_fn_t fn, void *ctx);

/**
 * @brief  全量快照分包上报
 * @param  apply_filter true 走白名单；false 忽略白名单
 * @retval SW_OK           各包均已交给链路；无合格点或空对象不发
 * @retval SW_ERR_NOT_INIT 物模型尚未注册
 * @note   须在链路已注册且具备 publish_properties_json 时才会上行。
 */
sw_err_t cloud_json_publish_snapshot(bool apply_filter);

/**
 * @brief  指定 id 分包上报（受白名单过滤）
 * @param  ids   点位标识列表
 * @param  count 列表长度
 * @retval SW_OK        各包均已交给链路；过滤后为空不发
 * @retval SW_ERR_PARAM 参数无效
 */
sw_err_t cloud_json_publish_delta(const char *const *ids, size_t count);

/**
 * @brief  应用属性下发 JSON（供测试或入站适配器直连使用）
 * @param  result 应用结果输出，可为 NULL
 */
sw_err_t cloud_json_apply_property_set(const char *json_str, point_apply_result_t *result);

#ifdef __cplusplus
}
#endif

#endif /* ADAPTERS_OUTBOUND_CLOUD_CLOUD_JSON_H */
