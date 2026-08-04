/**
 * @file    cloud_point_dispatch.c
 * @brief   云端物模型点位 dispatch 与 JSON 编解码实现
 * @author  HUWANGWEI
 * @date    2026-07-08
 */

#include "cloud/cloud_point.h"
#include "common/log.h"
#include "third_party/cJSON/cJSON.h"

#include <string.h>

static cloud_point_get_fail_policy_t s_get_fail_policy   = CLOUD_POINT_GET_FAIL_OMIT;
static cloud_device_cmd_submit_fn_t  s_device_cmd_submit = NULL;

void cloud_point_set_get_fail_policy(cloud_point_get_fail_policy_t policy)
{
    s_get_fail_policy = policy;
}

void cloud_point_set_device_cmd_submit(cloud_device_cmd_submit_fn_t fn)
{
    s_device_cmd_submit = fn;
}

/**
 * @brief  脉冲命令回显空闲态（恒定 false）
 */
sw_err_t cloud_point_get_echo_idle(point_value_t *out)
{
    if (out == NULL) {
        return SW_ERR_PARAM;
    }

    out->b = false;
    return SW_OK;
}

/**
 * @brief  经 cloud_device_cmd_submit 回调提交标准设备命令
 */
static sw_err_t submit_device_cmd(dev_cmd_kind_t kind)
{
    if (s_device_cmd_submit == NULL) {
        LOG_WARN("cloud_point: device_cmd submit handler not registered");
        return SW_ERR_NOT_INIT;
    }

    if (kind == DEV_CMD_NONE) {
        return SW_ERR_PARAM;
    }

    return s_device_cmd_submit(kind);
}

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

static sw_err_t dispatch_set(const cloud_point_entry_t *entry, const point_value_t *val, point_apply_result_t *result)
{
    sw_err_t ret = SW_OK;

    switch (entry->semantic) {
    case CLOUD_POINT_SEM_TELEMETRY:
        LOG_WARN("cloud_point: id=%s telemetry is read-only", entry->base.id);
        point_apply_result_record_error(result, entry->base.id, SW_ERR_STATE);
        return SW_ERR_STATE;

    case CLOUD_POINT_SEM_DEVICE_CMD:
        if (!val->b) {
            return SW_OK;
        }
        ret = submit_device_cmd(entry->cmd_kind);
        if (ret != SW_OK) {
            point_apply_result_record_error(result, entry->base.id, ret);
        }
        return ret;

    case CLOUD_POINT_SEM_CLOUD_SERVICE:
        if (entry->service == NULL) {
            point_apply_result_record_error(result, entry->base.id, SW_ERR_NOT_INIT);
            return SW_ERR_NOT_INIT;
        }
        ret = entry->service(val);
        if (ret != SW_OK) {
            point_apply_result_record_error(result, entry->base.id, ret);
        }
        return ret;

    case CLOUD_POINT_SEM_MANUAL_ACT:
        if (entry->base.set == NULL) {
            point_apply_result_record_error(result, entry->base.id, SW_ERR_NOT_INIT);
            return SW_ERR_NOT_INIT;
        }
        ret = entry->base.set(val);
        if (ret != SW_OK) {
            point_apply_result_record_error(result, entry->base.id, ret);
        }
        return ret;

    default:
        point_apply_result_record_error(result, entry->base.id, SW_ERR_PARAM);
        return SW_ERR_PARAM;
    }
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
        LOG_WARN("cloud_point: invalid json: %.80s", json_str);
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
            LOG_WARN("cloud_point: unknown id=%s", item->string);
            if (result_opt != NULL) {
                result_opt->rejected++;
            }
            point_apply_result_record_error(result_opt, item->string, SW_ERR_PARAM);
            continue;
        }

        if ((entry->access == CLOUD_POINT_ACCESS_RO) || (entry->semantic == CLOUD_POINT_SEM_TELEMETRY)) {
            LOG_WARN("cloud_point: id=%s is read-only", entry->base.id);
            if (result_opt != NULL) {
                result_opt->rejected++;
            }
            point_apply_result_record_error(result_opt, entry->base.id, SW_ERR_STATE);
            continue;
        }

        ret = point_table_parse_cjson_value(entry->base.type, item, &val);
        if (ret != SW_OK) {
            LOG_WARN("cloud_point: id=%s type mismatch", entry->base.id);
            if (result_opt != NULL) {
                result_opt->rejected++;
            }
            point_apply_result_record_error(result_opt, entry->base.id, ret);
            continue;
        }

        ret = dispatch_set(entry, &val, result_opt);
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
