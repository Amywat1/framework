/**
 * @file    point_table_to_json.c
 * @brief   标识符点位表 JSON 序列化实现
 * @author  HUWANGWEI
 * @date    2026-07-02
 */

#include "common/point_table/point_table.h"
#include "third_party/cJSON/cJSON.h"

#include <stdlib.h>
#include <string.h>

static sw_err_t finalize_json(cJSON *root, char *buf, size_t buf_size)
{
    char    *json;
    sw_err_t ret = SW_ERR_PARAM;

    json = cJSON_PrintUnformatted(root);
    if (json != NULL) {
        size_t len = strlen(json);

        if (len < buf_size) {
            memcpy(buf, json, len + 1U);
            ret = SW_OK;
        }
        free(json);
    }

    return ret;
}

static void append_typed_value(cJSON *root, const point_table_entry_t *entry, const point_value_t *val)
{
    switch (entry->type) {
    case POINT_TYPE_BOOL:
        cJSON_AddBoolToObject(root, entry->id, val->b);
        break;
    case POINT_TYPE_INT:
        cJSON_AddNumberToObject(root, entry->id, (double)val->i);
        break;
    case POINT_TYPE_FLOAT:
        cJSON_AddNumberToObject(root, entry->id, (double)val->f);
        break;
    case POINT_TYPE_STRING:
        cJSON_AddStringToObject(root, entry->id, val->s);
        break;
    default:
        break;
    }
}

static sw_err_t append_entry(cJSON                     *root,
                             const point_table_entry_t *entry,
                             point_get_fail_policy_t    fail_policy,
                             point_apply_result_t      *result_opt)
{
    point_value_t val;
    sw_err_t      ret;

    if (entry->get == NULL) {
        return SW_OK;
    }

    ret = entry->get(&val);
    if (ret != SW_OK) {
        if (result_opt != NULL) {
            result_opt->skipped_get++;
        }

        switch (fail_policy) {
        case POINT_GET_FAIL_ABORT:
            return SW_ERR_PARAM;
        case POINT_GET_FAIL_NULL:
            cJSON_AddNullToObject(root, entry->id);
            return SW_OK;
        case POINT_GET_FAIL_OMIT:
        default:
            return SW_OK;
        }
    }

    append_typed_value(root, entry, &val);
    return SW_OK;
}

sw_err_t point_table_to_json_ex(const point_table_entry_t *entries,
                                size_t                     count,
                                char                      *buf,
                                size_t                     buf_size,
                                point_get_fail_policy_t    fail_policy,
                                point_apply_result_t      *result_opt)
{
    cJSON   *root = cJSON_CreateObject();
    sw_err_t ret  = SW_ERR_PARAM;

    point_apply_result_init(result_opt);

    if (root == NULL) {
        return SW_ERR_PARAM;
    }

    for (size_t i = 0U; i < count; i++) {
        ret = append_entry(root, &entries[i], fail_policy, result_opt);
        if (ret != SW_OK) {
            cJSON_Delete(root);
            return ret;
        }
    }

    ret = finalize_json(root, buf, buf_size);
    cJSON_Delete(root);
    return ret;
}

sw_err_t point_table_to_json(const point_table_entry_t *entries, size_t count, char *buf, size_t buf_size)
{
    return point_table_to_json_ex(entries, count, buf, buf_size, POINT_GET_FAIL_OMIT, NULL);
}

static const point_table_entry_t *find_entry_by_id(const point_table_entry_t *entries, size_t count, const char *id)
{
    for (size_t i = 0U; i < count; i++) {
        if ((entries[i].id != NULL) && (strcmp(entries[i].id, id) == 0)) {
            return &entries[i];
        }
    }
    return NULL;
}

sw_err_t point_table_to_json_filtered(const point_table_entry_t *entries,
                                      size_t                     count,
                                      const char *const         *ids,
                                      size_t                     id_count,
                                      char                      *buf,
                                      size_t                     buf_size)
{
    cJSON   *root       = cJSON_CreateObject();
    sw_err_t ret        = SW_ERR_PARAM;
    size_t   serialized = 0U;

    if ((root == NULL) || (ids == NULL) || (id_count == 0U)) {
        if (root != NULL) {
            cJSON_Delete(root);
        }
        return SW_ERR_PARAM;
    }

    for (size_t j = 0U; j < id_count; j++) {
        const point_table_entry_t *entry;
        point_value_t              val;

        if (ids[j] == NULL) {
            continue;
        }

        entry = find_entry_by_id(entries, count, ids[j]);
        if ((entry == NULL) || (entry->get == NULL) || (entry->get(&val) != SW_OK)) {
            continue;
        }

        append_typed_value(root, entry, &val);
        serialized++;
    }

    if (serialized == 0U) {
        cJSON_Delete(root);
        return SW_ERR_PARAM;
    }

    ret = finalize_json(root, buf, buf_size);
    cJSON_Delete(root);
    return ret;
}
