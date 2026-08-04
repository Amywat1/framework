/**
 * @file    point_table_json.h
 * @brief   点位表的 JSON 序列化与反序列化
 * @author  HUWANGWEI
 * @date    2026-08-04
 *
 * @note    与 `common/point_table/point_table.h` 的分工：后者定义点位表模型
 *          （标识符 + 类型 + get/set）与查表，与序列化格式无关；本文件是其
 *          JSON 适配，是本框架当前唯一的点位表编解码实现。
 *
 * @note    为何不放在 common/：`common/` 应对上层与外部格式均无依赖。编解码
 *          留在 common 会让最底层绑定一种序列化格式，并使边界规则 R9b
 *          （domain 不解析序列化格式）失去基础——domain 依赖 common，
 *          而 common 自己 include cJSON，禁令就只是一条孤立的例外。
 *
 * @note    需要另一种编码（如 CBOR、protobuf）时，在本目录并列新增实现，
 *          点位表模型不必改动。
 */

#ifndef ADAPTERS_OUTBOUND_SERIALIZATION_JSON_POINT_TABLE_JSON_H
#define ADAPTERS_OUTBOUND_SERIALIZATION_JSON_POINT_TABLE_JSON_H

#include "common/point_table/point_table.h"

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

struct cJSON;

/**
 * @brief  按点位类型从 cJSON 节点解析值
 * @param  type  点位类型
 * @param  item  JSON 节点
 * @param  out   输出值
 * @retval SW_OK 解析成功
 */
sw_err_t point_table_parse_cjson_value(point_type_t type, const struct cJSON *item, point_value_t *out);

/**
 * @brief  遍历点表，将所有 get!=NULL 的点位序列化为 JSON
 */
sw_err_t point_table_to_json(const point_table_entry_t *entries, size_t count, char *buf, size_t buf_size);

/**
 * @brief  遍历点表序列化，并可选统计 get 失败数
 */
sw_err_t point_table_to_json_ex(const point_table_entry_t *entries,
                                size_t                     count,
                                char                      *buf,
                                size_t                     buf_size,
                                point_get_fail_policy_t    fail_policy,
                                point_apply_result_t      *result_opt);

/**
 * @brief  将指定 id 列表对应的可读点位序列化为 JSON
 */
sw_err_t point_table_to_json_filtered(const point_table_entry_t *entries,
                                      size_t                     count,
                                      const char *const         *ids,
                                      size_t                     id_count,
                                      char                      *buf,
                                      size_t                     buf_size);

/**
 * @brief  解析 JSON 并逐 key 调用 set()，汇总处理结果
 */
sw_err_t point_table_apply_json(const point_table_entry_t *entries,
                                size_t                     count,
                                const char                *json_str,
                                point_apply_result_t      *result_opt);

/**
 * @brief  解析 JSON 并逐 key 调用 set()（兼容入口，不返回结果）
 */
void point_table_from_json(const point_table_entry_t *entries, size_t count, const char *json_str);

#ifdef __cplusplus
}
#endif

#endif /* ADAPTERS_OUTBOUND_SERIALIZATION_JSON_POINT_TABLE_JSON_H */
