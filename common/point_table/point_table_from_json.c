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

static const point_table_entry_t *find_entry(const char *id, const point_table_entry_t *entries, size_t count)
{
    for (size_t i = 0U; i < count; i++) {
        if ((entries[i].id != NULL) && (strcmp(entries[i].id, id) == 0)) {
            return &entries[i];
        }
    }
    return NULL;
}

static void record_error(point_apply_result_t *result, const char *id, sw_err_t err)
{
    if ((result == NULL) || (result->first_error != SW_OK)) {
        return;
    }

    result->first_error = err;
    if (id != NULL) {
        strncpy(result->first_error_id, id, sizeof(result->first_error_id) - 1U);
    }
}

static sw_err_t dispatch_one(const point_table_entry_t *entry, const cJSON *item, point_apply_result_t *result)
{
    point_value_t val;
    sw_err_t      ret;

    memset(&val, 0, sizeof(val));

    switch (entry->type) {
    case POINT_TYPE_BOOL:
        if (!cJSON_IsBool(item) && !cJSON_IsNumber(item)) {
            LOG_WARN("point_table: id=%s expect bool", entry->id);
            record_error(result, entry->id, SW_ERR_PARAM);
            return SW_ERR_PARAM;
        }
        val.b = cJSON_IsTrue(item) || (cJSON_IsNumber(item) && (item->valuedouble != 0.0));
        break;

    case POINT_TYPE_INT:
        if (!cJSON_IsNumber(item)) {
            LOG_WARN("point_table: id=%s expect number", entry->id);
            record_error(result, entry->id, SW_ERR_PARAM);
            return SW_ERR_PARAM;
        }
        val.i = (int32_t)item->valuedouble;
        break;

    case POINT_TYPE_STRING:
        if (!cJSON_IsString(item) || (item->valuestring == NULL)) {
            LOG_WARN("point_table: id=%s expect string", entry->id);
            record_error(result, entry->id, SW_ERR_PARAM);
            return SW_ERR_PARAM;
        }
        strncpy(val.s, item->valuestring, sizeof(val.s) - 1U);
        break;

    case POINT_TYPE_FLOAT:
        if (!cJSON_IsNumber(item)) {
            LOG_WARN("point_table: id=%s expect number", entry->id);
            record_error(result, entry->id, SW_ERR_PARAM);
            return SW_ERR_PARAM;
        }
        val.f = (float)item->valuedouble;
        break;

    default:
        record_error(result, entry->id, SW_ERR_PARAM);
        return SW_ERR_PARAM;
    }

    if (entry->set == NULL) {
        LOG_WARN("point_table: id=%s is read-only", entry->id);
        record_error(result, entry->id, SW_ERR_STATE);
        return SW_ERR_STATE;
    }

    ret = entry->set(&val);
    if (ret != SW_OK) {
        LOG_WARN("point_table: id=%s set failed ret=%d", entry->id, (int)ret);
        record_error(result, entry->id, ret);
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
        record_error(result_opt, "", SW_ERR_PARAM);
        return SW_ERR_PARAM;
    }

    root = cJSON_Parse(json_str);
    if (root == NULL) {
        LOG_WARN("point_table: invalid json: %.80s", json_str);
        record_error(result_opt, "", SW_ERR_PARAM);
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

        entry = find_entry(item->string, entries, count);
        if (entry == NULL) {
            LOG_WARN("point_table: unknown id=%s", item->string);
            if (result_opt != NULL) {
                result_opt->rejected++;
            }
            record_error(result_opt, item->string, SW_ERR_PARAM);
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
