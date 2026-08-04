/**
 * @file    cloud_point_json.c
 * @brief   云端物模型点位表的 JSON 编解码
 * @author  HUWANGWEI
 * @date    2026-08-04
 *
 * @note    与 `domain/cloud/cloud_point_dispatch.c` 的分工：本文件只做 JSON 与
 *          点位值之间的转换，转换后逐个交给 `cloud_point_apply_value()` 按
 *          语义分派。语义规则（DEVICE_CMD 提交命令、只读点位拒绝写入等）
 *          属领域逻辑，不在此处。
 *
 * @note    为何拆开：原先编解码与语义分派同在 `cloud/`，使该目录依赖
 *          `adapters/`（违反边界规则 R12）。更根本的原因是这让点位表的
 *          语义规则与一种具体传输格式绑定——换协议就要改分派代码。
 */

#include "adapters/outbound/cloud/cloud_point_json.h"

#include "adapters/outbound/serialization/json/point_table_json.h"
#include "common/log.h"
#include "third_party/cJSON/cJSON.h"

#include <stddef.h>

static cloud_point_get_fail_policy_t s_get_fail_policy = CLOUD_POINT_GET_FAIL_OMIT;

void cloud_point_set_get_fail_policy(cloud_point_get_fail_policy_t policy)
{
    s_get_fail_policy = policy;
}

/* 上行只取可读点位的 base，交给通用点表序列化 */
static sw_err_t collect_readable_bases(const cloud_point_entry_t *entries,
                                       size_t                     count,
                                       point_table_entry_t       *out,
                                       size_t                     out_cap,
                                       size_t                    *out_count)
{
    size_t n = 0U;

    if (count > out_cap) {
        return SW_ERR_OVERFLOW;
    }

    for (size_t i = 0U; i < count; i++) {
        if (entries[i].base.get == NULL) {
            continue;
        }
        out[n++] = entries[i].base;
    }

    *out_count = n;
    return SW_OK;
}

sw_err_t cloud_point_to_json(const cloud_point_entry_t *entries, size_t count, char *buf, size_t buf_size)
{
    point_table_entry_t table[CLOUD_POINT_TABLE_MAX];
    size_t              n = 0U;
    sw_err_t            ret;

    if ((entries == NULL) || (count == 0U) || (buf == NULL) || (buf_size == 0U)) {
        return SW_ERR_PARAM;
    }

    ret = collect_readable_bases(entries, count, table, CLOUD_POINT_TABLE_MAX, &n);
    if (ret != SW_OK) {
        return ret;
    }

    return point_table_to_json_ex(table, n, buf, buf_size, s_get_fail_policy, NULL);
}

sw_err_t cloud_point_to_json_filtered(const cloud_point_entry_t *entries,
                                      size_t                     count,
                                      const char *const         *ids,
                                      size_t                     id_count,
                                      char                      *buf,
                                      size_t                     buf_size)
{
    point_table_entry_t table[CLOUD_POINT_TABLE_MAX];
    size_t              n = 0U;
    sw_err_t            ret;

    if ((entries == NULL) || (count == 0U) || (ids == NULL) || (id_count == 0U)) {
        return SW_ERR_PARAM;
    }

    ret = collect_readable_bases(entries, count, table, CLOUD_POINT_TABLE_MAX, &n);
    if (ret != SW_OK) {
        return ret;
    }

    return point_table_to_json_filtered(table, n, ids, id_count, buf, buf_size);
}

sw_err_t cloud_point_apply_json(const cloud_point_entry_t *entries,
                                size_t                     count,
                                const char                *json_str,
                                point_apply_result_t      *result_opt)
{
    cJSON *root;
    cJSON *item;

    point_apply_result_init(result_opt);

    if ((entries == NULL) || (count == 0U) || (json_str == NULL)) {
        point_apply_result_record_error(result_opt, "", SW_ERR_PARAM);
        return SW_ERR_PARAM;
    }

    root = cJSON_Parse(json_str);
    if (root == NULL) {
        LOG_WARN("cloud_point_json: invalid json: %.80s", json_str);
        point_apply_result_record_error(result_opt, "", SW_ERR_PARAM);
        return SW_ERR_PARAM;
    }

    cJSON_ArrayForEach(item, root)
    {
        const cloud_point_entry_t *entry;
        point_value_t              val;
        sw_err_t                   ret;

        if (result_opt != NULL) {
            result_opt->total_keys++;
        }

        if (item->string == NULL) {
            continue;
        }

        entry = (const cloud_point_entry_t *)point_table_find_entry_at(
            entries, count, sizeof(cloud_point_entry_t), item->string);
        if (entry == NULL) {
            LOG_WARN("cloud_point_json: unknown id=%s", item->string);
            if (result_opt != NULL) {
                result_opt->rejected++;
            }
            point_apply_result_record_error(result_opt, item->string, SW_ERR_PARAM);
            continue;
        }

        /* 类型不符属编解码问题，在此拒绝；能否写入由语义分派回答 */
        ret = point_table_parse_cjson_value(entry->base.type, item, &val);
        if (ret != SW_OK) {
            LOG_WARN("cloud_point_json: id=%s type mismatch", entry->base.id);
            if (result_opt != NULL) {
                result_opt->rejected++;
            }
            point_apply_result_record_error(result_opt, entry->base.id, ret);
            continue;
        }

        ret = cloud_point_apply_value(entry, &val, result_opt);
        if (ret == SW_OK) {
            if (result_opt != NULL) {
                result_opt->applied++;
            }
        } else if (result_opt != NULL) {
            result_opt->rejected++;
        }
    }

    cJSON_Delete(root);
    return SW_OK;
}
