/**
 * @file    point_table_from_json.c
 * @brief   标识符点位表 JSON 反序列化实现
 * @author  HUWANGWEI
 * @date    2026-07-02
 */

#include "common/log.h"
#include "common/point_table/point_table.h"
#include "third_party/cJSON/cJSON.h"

#include <string.h>

void point_apply_result_init(point_apply_result_t *result)
{
    if (result != NULL) {
        memset(result, 0, sizeof(*result));
    }
}

void point_apply_result_record_error(point_apply_result_t *result, const char *id, sw_err_t err)
{
    if ((result == NULL) || (result->first_error != SW_OK)) {
        return;
    }

    result->first_error = err;
    if (id != NULL) {
        strncpy(result->first_error_id, id, sizeof(result->first_error_id) - 1U);
    }
}

const point_table_entry_t *point_table_find_entry_at(const void *entries,
                                                     size_t      count,
                                                     size_t      entry_stride,
                                                     const char *id)
{
    const char *row;

    if ((entries == NULL) || (id == NULL) || (entry_stride == 0U)) {
        return NULL;
    }

    for (size_t i = 0U; i < count; i++) {
        row = ((const char *)entries) + (i * entry_stride);
        if ((((const point_table_entry_t *)row)->id != NULL)
            && (strcmp(((const point_table_entry_t *)row)->id, id) == 0)) {
            return (const point_table_entry_t *)row;
        }
    }
    return NULL;
}

const point_table_entry_t *point_table_find_entry(const point_table_entry_t *entries, size_t count, const char *id)
{
    return point_table_find_entry_at(entries, count, sizeof(point_table_entry_t), id);
}

sw_err_t point_table_parse_cjson_value(point_type_t type, const struct cJSON *item, point_value_t *out)
{
    const cJSON *node = (const cJSON *)item;

    if ((node == NULL) || (out == NULL)) {
        return SW_ERR_PARAM;
    }

    memset(out, 0, sizeof(*out));

    switch (type) {
    case POINT_TYPE_BOOL:
        if (!cJSON_IsBool(node) && !cJSON_IsNumber(node)) {
            return SW_ERR_PARAM;
        }
        out->b = cJSON_IsTrue(node) || (cJSON_IsNumber(node) && (node->valuedouble != 0.0));
        return SW_OK;

    case POINT_TYPE_INT:
        if (!cJSON_IsNumber(node)) {
            return SW_ERR_PARAM;
        }
        out->i = (int32_t)node->valuedouble;
        return SW_OK;

    case POINT_TYPE_STRING:
        if (!cJSON_IsString(node) || (node->valuestring == NULL)) {
            return SW_ERR_PARAM;
        }
        strncpy(out->s, node->valuestring, sizeof(out->s) - 1U);
        return SW_OK;

    case POINT_TYPE_FLOAT:
        if (!cJSON_IsNumber(node)) {
            return SW_ERR_PARAM;
        }
        out->f = (float)node->valuedouble;
        return SW_OK;

    default:
        return SW_ERR_PARAM;
    }
}

static sw_err_t dispatch_one(const point_table_entry_t *entry, const cJSON *item, point_apply_result_t *result)
{
    point_value_t val;
    sw_err_t      ret;

    ret = point_table_parse_cjson_value(entry->type, item, &val);
    if (ret != SW_OK) {
        LOG_WARN("point_table: id=%s type mismatch", entry->id);
        point_apply_result_record_error(result, entry->id, ret);
        return ret;
    }

    if (entry->set == NULL) {
        LOG_WARN("point_table: id=%s is read-only", entry->id);
        point_apply_result_record_error(result, entry->id, SW_ERR_STATE);
        return SW_ERR_STATE;
    }

    ret = entry->set(&val);
    if (ret != SW_OK) {
        LOG_WARN("point_table: id=%s set failed ret=%d", entry->id, (int)ret);
        point_apply_result_record_error(result, entry->id, ret);
        return ret;
    }

    return SW_OK;
}

sw_err_t point_table_apply_json(const point_table_entry_t *entries,
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
        LOG_WARN("point_table: invalid json: %.80s", json_str);
        point_apply_result_record_error(result_opt, "", SW_ERR_PARAM);
        return SW_ERR_PARAM;
    }

    cJSON_ArrayForEach(item, root)
    {
        const point_table_entry_t *entry;

        if (result_opt != NULL) {
            result_opt->total_keys++;
        }

        if (item->string == NULL) {
            continue;
        }

        entry = point_table_find_entry(entries, count, item->string);
        if (entry == NULL) {
            LOG_WARN("point_table: unknown id=%s", item->string);
            if (result_opt != NULL) {
                result_opt->rejected++;
            }
            point_apply_result_record_error(result_opt, item->string, SW_ERR_PARAM);
            continue;
        }

        if (dispatch_one(entry, item, result_opt) == SW_OK) {
            if (result_opt != NULL) {
                result_opt->applied++;
            }
        } else if (result_opt != NULL) {
            result_opt->rejected++;
        }
    }

    cJSON_Delete(root);

    if ((result_opt != NULL) && (result_opt->applied == 0U) && (result_opt->total_keys > 0U)) {
        return SW_ERR_PARAM;
    }

    return SW_OK;
}

void point_table_from_json(const point_table_entry_t *entries, size_t count, const char *json_str)
{
    (void)point_table_apply_json(entries, count, json_str, NULL);
}
