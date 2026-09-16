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
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

/* 上行只取快照策略点位的 base，交给通用点表序列化 */
static sw_err_t collect_snapshot_bases(const cloud_point_entry_t *entries,
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
        if (!cloud_point_snapshot_enabled(&entries[i])) {
            continue;
        }
        out[n++] = entries[i].base;
    }

    *out_count = n;
    return SW_OK;
}

static sw_err_t write_empty_object(char *buf, size_t buf_size)
{
    if (buf_size < 3U) {
        return SW_ERR_OVERFLOW;
    }
    buf[0] = '{';
    buf[1] = '}';
    buf[2] = '\0';
    return SW_OK;
}

sw_err_t cloud_point_to_json(const cloud_point_entry_t *entries, size_t count, char *buf, size_t buf_size)
{
    point_table_entry_t *table;
    size_t               n = 0U;
    sw_err_t             ret;

    if ((entries == NULL) || (count == 0U) || (buf == NULL) || (buf_size == 0U)) {
        return SW_ERR_PARAM;
    }

    table = malloc(sizeof(*table) * CLOUD_POINT_TABLE_MAX);
    if (table == NULL) {
        return SW_ERR_NOMEM;
    }

    ret = collect_snapshot_bases(entries, count, table, CLOUD_POINT_TABLE_MAX, &n);
    if (ret != SW_OK) {
        free(table);
        return ret;
    }
    if (n == 0U) {
        free(table);
        return write_empty_object(buf, buf_size);
    }

    ret = point_table_to_json_ex(table, n, buf, buf_size, POINT_GET_FAIL_OMIT, NULL);
    free(table);
    return ret;
}

sw_err_t cloud_point_to_json_filtered(const cloud_point_entry_t *entries,
                                      size_t                     count,
                                      const char *const         *ids,
                                      size_t                     id_count,
                                      char                      *buf,
                                      size_t                     buf_size)
{
    point_table_entry_t *table;
    size_t               n = 0U;
    sw_err_t             ret;

    if ((entries == NULL) || (count == 0U) || (ids == NULL) || (id_count == 0U)) {
        return SW_ERR_PARAM;
    }

    table = malloc(sizeof(*table) * CLOUD_POINT_TABLE_MAX);
    if (table == NULL) {
        return SW_ERR_NOMEM;
    }

    ret = collect_snapshot_bases(entries, count, table, CLOUD_POINT_TABLE_MAX, &n);
    if (ret != SW_OK) {
        free(table);
        return ret;
    }
    if (n == 0U) {
        free(table);
        return SW_ERR_PARAM;
    }

    ret = point_table_to_json_filtered(table, n, ids, id_count, buf, buf_size);
    free(table);
    return ret;
}

static const cloud_point_entry_t *find_cloud_entry(const cloud_point_entry_t *entries, size_t count, const char *id)
{
    return (const cloud_point_entry_t *)point_table_find_entry_at(entries, count, sizeof(cloud_point_entry_t), id);
}

static sw_err_t dump_cjson(cJSON *obj, char *buf, size_t buf_size)
{
    char    *json;
    sw_err_t ret = SW_ERR_OVERFLOW;

    json = cJSON_PrintUnformatted(obj);
    if (json == NULL) {
        return SW_ERR_NOMEM;
    }
    if (strlen(json) < buf_size) {
        memcpy(buf, json, strlen(json) + 1U);
        ret = SW_OK;
    }
    free(json);
    return ret;
}

static void append_typed_number(cJSON *root, const cloud_point_entry_t *entry, const point_value_t *val)
{
    switch (entry->base.type) {
    case POINT_TYPE_BOOL:
        cJSON_AddNumberToObject(root, entry->base.id, val->b ? 1.0 : 0.0);
        break;
    case POINT_TYPE_INT:
        cJSON_AddNumberToObject(root, entry->base.id, (double)val->i);
        break;
    case POINT_TYPE_FLOAT:
        cJSON_AddNumberToObject(root, entry->base.id, (double)val->f);
        break;
    case POINT_TYPE_STRING:
        cJSON_AddStringToObject(root, entry->base.id, val->s);
        break;
    default:
        break;
    }
}

static bool id_in_list(const char *id, const char *const *ids, size_t count)
{
    size_t i;

    if ((id == NULL) || (ids == NULL)) {
        return false;
    }

    for (i = 0U; i < count; i++) {
        if ((ids[i] != NULL) && (strcmp(ids[i], id) == 0)) {
            return true;
        }
    }
    return false;
}

static void append_current_value(cJSON *root, const cloud_point_entry_t *entry)
{
    point_value_t now;

    if ((entry == NULL) || (entry->base.get == NULL)) {
        return;
    }
    if (entry->base.get(&now) != SW_OK) {
        return;
    }
    append_typed_number(root, entry, &now);
}

sw_err_t cloud_point_to_json_downlink(const cloud_point_entry_t *entries,
                                      size_t                     count,
                                      const char                *request_json,
                                      const char *const         *applied_ids,
                                      size_t                     applied_count,
                                      char                      *echo_buf,
                                      size_t                     echo_size,
                                      char                      *idle_buf,
                                      size_t                     idle_size)
{
    cJSON   *root;
    cJSON   *echo;
    cJSON   *idle;
    cJSON   *item;
    sw_err_t ret;

    if ((entries == NULL) || (count == 0U) || (request_json == NULL) || (echo_buf == NULL) || (echo_size == 0U)
        || (idle_buf == NULL) || (idle_size == 0U)) {
        return SW_ERR_PARAM;
    }
    if ((applied_count > 0U) && (applied_ids == NULL)) {
        return SW_ERR_PARAM;
    }

    root = cJSON_Parse(request_json);
    if ((root == NULL) || !cJSON_IsObject(root)) {
        cJSON_Delete(root);
        return SW_ERR_PARAM;
    }

    echo = cJSON_CreateObject();
    idle = cJSON_CreateObject();
    if ((echo == NULL) || (idle == NULL)) {
        cJSON_Delete(echo);
        cJSON_Delete(idle);
        cJSON_Delete(root);
        return SW_ERR_NOMEM;
    }

    cJSON_ArrayForEach(item, root)
    {
        const cloud_point_entry_t *entry;
        point_value_t              val;
        bool                       applied;

        if ((item->string == NULL) || (item->string[0] == '\0')) {
            continue;
        }
        entry = find_cloud_entry(entries, count, item->string);
        if (!cloud_point_snapshot_enabled(entry)) {
            continue;
        }

        applied = id_in_list(entry->base.id, applied_ids, applied_count);
        if (applied && (entry->kind != CLOUD_KIND_TELEMETRY)) {
            if (point_table_parse_cjson_value(entry->base.type, item, &val) != SW_OK) {
                continue;
            }
            append_typed_number(echo, entry, &val);
            if (cloud_point_is_pulse(entry) && (entry->base.type == POINT_TYPE_BOOL) && val.b) {
                cJSON_AddNumberToObject(idle, entry->base.id, 0.0);
            }
            continue;
        }
        append_current_value(echo, entry);
    }

    ret = dump_cjson(echo, echo_buf, echo_size);
    if (ret == SW_OK) {
        ret = dump_cjson(idle, idle_buf, idle_size);
    }
    cJSON_Delete(echo);
    cJSON_Delete(idle);
    cJSON_Delete(root);
    return ret;
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

    if (strlen(json_str) > CLOUD_REPORT_JSON_MAX) {
        LOG_WARN("cloud_point_json: payload too long");
        point_apply_result_record_error(result_opt, "", SW_ERR_OVERFLOW);
        return SW_ERR_OVERFLOW;
    }

    root = cJSON_Parse(json_str);
    if (root == NULL) {
        LOG_WARN("cloud_point_json: invalid json: %.80s", json_str);
        point_apply_result_record_error(result_opt, "", SW_ERR_PARAM);
        return SW_ERR_PARAM;
    }

    if (!cJSON_IsObject(root)) {
        LOG_WARN("cloud_point_json: root is not an object");
        cJSON_Delete(root);
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

        entry = find_cloud_entry(entries, count, item->string);
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
                if (result_opt->applied_id_count < POINT_APPLY_IDS_MAX) {
                    result_opt->applied_ids[result_opt->applied_id_count++] = entry->base.id;
                }
            }
        } else if (result_opt != NULL) {
            result_opt->rejected++;
        }
    }

    cJSON_Delete(root);
    return SW_OK;
}
