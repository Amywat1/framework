/**
 * @file    cloud_point_json.h
 * @brief   云端物模型点位表的 JSON 编解码
 * @author  HUWANGWEI
 * @date    2026-08-04
 *
 * @note    上行把可读点位序列化为属性 JSON，下行解析属性载荷后逐个交给
 *          `cloud_point_apply_value()` 按语义分派。语义规则本身在
 *          `domain/cloud/cloud_point_dispatch.c`，不在此处。
 */

#ifndef ADAPTERS_OUTBOUND_CLOUD_CLOUD_POINT_JSON_H
#define ADAPTERS_OUTBOUND_CLOUD_CLOUD_POINT_JSON_H

#include "domain/cloud/cloud_point.h"

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief  设置上行 get 失败时的处理策略
 * @param  policy  跳过该 key / 整包失败 / 写入 null
 */
void cloud_point_set_get_fail_policy(cloud_point_get_fail_policy_t policy);

/**
 * @brief  将全部可读点位序列化为属性 JSON
 * @retval SW_OK           成功
 * @retval SW_ERR_PARAM    入参非法
 * @retval SW_ERR_OVERFLOW 点位数超出 CLOUD_POINT_TABLE_MAX
 */
sw_err_t cloud_point_to_json(const cloud_point_entry_t *entries, size_t count, char *buf, size_t buf_size);

/**
 * @brief  只序列化 ids 列表中的点位（增量上报）
 */
sw_err_t cloud_point_to_json_filtered(const cloud_point_entry_t *entries,
                                      size_t                     count,
                                      const char *const         *ids,
                                      size_t                     id_count,
                                      char                      *buf,
                                      size_t                     buf_size);

/**
 * @brief  解析属性下发 JSON 并逐 key 按语义分派
 * @param  result_opt 逐 key 处理结果汇总，可为 NULL
 * @retval SW_OK        载荷解析成功（单个 key 的失败记入 result_opt，不影响返回值）
 * @retval SW_ERR_PARAM 入参非法或 JSON 无法解析
 * @note   单个 key 失败不中断其余 key：属性下发是批量操作，一个非法字段
 *         不应让整批合法字段一起丢弃。
 */
sw_err_t cloud_point_apply_json(const cloud_point_entry_t *entries,
                                size_t                     count,
                                const char                *json_str,
                                point_apply_result_t      *result_opt);

#ifdef __cplusplus
}
#endif

#endif /* ADAPTERS_OUTBOUND_CLOUD_CLOUD_POINT_JSON_H */
