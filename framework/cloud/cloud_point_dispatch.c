/**
 * @file    cloud_point_dispatch.c
 * @brief   云端物模型点位 dispatch 与 JSON 编解码实现
 * @author  HUWANGWEI
 * @date    2026-07-08
 */

#include "framework/cloud/cloud_point.h"
#include "framework/ports/inbound/command/command_port.h"
#include "framework/common/log.h"
#include "third_party/cJSON/cJSON.h"
#include <string.h>

static cloud_point_get_fail_policy_t s_get_fail_policy = CLOUD_POINT_GET_FAIL_OMIT;

void cloud_point_set_get_fail_policy(cloud_point_get_fail_policy_t policy)
{
    s_get_fail_policy = policy;
}

sw_err_t cloud_point_get_pulse_false(point_value_t *out)
{
    if (out == NULL)
    {
        return SW_ERR_PARAM;
    }

    out->b = false;
    return SW_OK;
}

static const cloud_point_entry_t *find_entry(const cloud_point_entry_t *entries,
                                                size_t count,
                                                const char *id)
{
    for (size_t i = 0U; i < count; i++)
    {
        if ((entries[i].base.id != NULL) && (strcmp(entries[i].base.id, id) == 0))
        {
            return &entries[i];
        }
    }
    return NULL;
}

static sw_err_t submit_device_cmd(cmd_type_t type)
{
    const command_port_ops_t *cp = command_port_get_ops();
    cmd_t                     cmd;

    if (cp == NULL)
    {
        LOG_WARN("cloud_point: command_port not registered");
        return SW_ERR_NOT_INIT;
    }

    if (type == CMD_NONE)
    {
        return SW_ERR_PARAM;
    }

    memset(&cmd, 0, sizeof(cmd));
    cmd.type = type;
    return cp->inject(&cmd);
}

static sw_err_t parse_value(const cloud_point_entry_t *entry,
                             const cJSON *item,
                             point_value_t *out)
{
    memset(out, 0, sizeof(*out));

    switch (entry->base.type)
    {
        case POINT_TYPE_BOOL:
            if (!cJSON_IsBool(item) && !cJSON_IsNumber(item))
            {
                return SW_ERR_PARAM;
            }
            out->b = cJSON_IsTrue(item) || (cJSON_IsNumber(item) && (item->valuedouble != 0.0));
            return SW_OK;

        case POINT_TYPE_INT:
            if (!cJSON_IsNumber(item))
            {
                return SW_ERR_PARAM;
            }
            out->i = (int32_t)item->valuedouble;
            return SW_OK;

        case POINT_TYPE_STRING:
            if (!cJSON_IsString(item) || (item->valuestring == NULL))
            {
                return SW_ERR_PARAM;
            }
            strncpy(out->s, item->valuestring, sizeof(out->s) - 1U);
            return SW_OK;

        case POINT_TYPE_FLOAT:
            if (!cJSON_IsNumber(item))
            {
                return SW_ERR_PARAM;
            }
            out->f = (float)item->valuedouble;
            return SW_OK;

        default:
            return SW_ERR_PARAM;
    }
}

static void record_error(point_apply_result_t *result,
                          const char *id,
                          sw_err_t err)
{
    if ((result == NULL) || (result->first_error != SW_OK))
    {
        return;
    }

    result->first_error = err;
    if (id != NULL)
    {
        strncpy(result->first_error_id, id, sizeof(result->first_error_id) - 1U);
    }
}

static sw_err_t dispatch_set(const cloud_point_entry_t *entry,
                              const point_value_t *val,
                              point_apply_result_t *result)
{
    sw_err_t ret = SW_OK;

    switch (entry->semantic)
    {
        case CLOUD_POINT_SEM_TELEMETRY:
            LOG_WARN("cloud_point: id=%s telemetry is read-only", entry->base.id);
            record_error(result, entry->base.id, SW_ERR_STATE);
            return SW_ERR_STATE;

        case CLOUD_POINT_SEM_DEVICE_CMD:
            if (!val->b)
            {
                return SW_OK;
            }
            ret = submit_device_cmd(entry->cmd_type);
            if (ret != SW_OK)
            {
                record_error(result, entry->base.id, ret);
            }
            return ret;

        case CLOUD_POINT_SEM_CLOUD_SERVICE:
            if (entry->service == NULL)
            {
                record_error(result, entry->base.id, SW_ERR_NOT_INIT);
                return SW_ERR_NOT_INIT;
            }
            ret = entry->service(val);
            if (ret != SW_OK)
            {
                record_error(result, entry->base.id, ret);
            }
            return ret;

        case CLOUD_POINT_SEM_MANUAL_ACT:
            if (entry->base.set == NULL)
            {
                record_error(result, entry->base.id, SW_ERR_NOT_INIT);
                return SW_ERR_NOT_INIT;
            }
            ret = entry->base.set(val);
            if (ret != SW_OK)
            {
                record_error(result, entry->base.id, ret);
            }
            return ret;

        default:
            record_error(result, entry->base.id, SW_ERR_PARAM);
            return SW_ERR_PARAM;
    }
}

sw_err_t cloud_point_apply_json(const cloud_point_entry_t *entries, size_t count,
                                 const char *json_str,
                                 point_apply_result_t *result_opt)
{
    cJSON *root;
    cJSON *item;

    point_apply_result_init(result_opt);

    if ((entries == NULL) || (count == 0U) || (json_str == NULL))
    {
        record_error(result_opt, "", SW_ERR_PARAM);
        return SW_ERR_PARAM;
    }

    root = cJSON_Parse(json_str);
    if (root == NULL)
    {
        LOG_WARN("cloud_point: invalid json: %.80s", json_str);
        record_error(result_opt, "", SW_ERR_PARAM);
        return SW_ERR_PARAM;
    }

    cJSON_ArrayForEach(item, root)
    {
        const cloud_point_entry_t *entry;
        point_value_t              val;
        sw_err_t                   ret;

        if (result_opt != NULL)
        {
            result_opt->total_keys++;
        }

        if (item->string == NULL)
        {
            continue;
        }

        entry = find_entry(entries, count, item->string);
        if (entry == NULL)
        {
            LOG_WARN("cloud_point: unknown id=%s", item->string);
            if (result_opt != NULL)
            {
                result_opt->rejected++;
            }
            record_error(result_opt, item->string, SW_ERR_PARAM);
            continue;
        }

        if ((entry->access == CLOUD_POINT_ACCESS_RO) ||
            (entry->semantic == CLOUD_POINT_SEM_TELEMETRY))
        {
            LOG_WARN("cloud_point: id=%s is read-only", entry->base.id);
            if (result_opt != NULL)
            {
                result_opt->rejected++;
            }
            record_error(result_opt, entry->base.id, SW_ERR_STATE);
            continue;
        }

        ret = parse_value(entry, item, &val);
        if (ret != SW_OK)
        {
            LOG_WARN("cloud_point: id=%s type mismatch", entry->base.id);
            if (result_opt != NULL)
            {
                result_opt->rejected++;
            }
            record_error(result_opt, entry->base.id, ret);
            continue;
        }

        ret = dispatch_set(entry, &val, result_opt);
        if (ret == SW_OK)
        {
            if (result_opt != NULL)
            {
                result_opt->applied++;
            }
        }
        else if (result_opt != NULL)
        {
            result_opt->rejected++;
        }
    }

    cJSON_Delete(root);
    return SW_OK;
}

sw_err_t cloud_point_to_json(const cloud_point_entry_t *entries, size_t count,
                              char *buf, size_t buf_size)
{
    point_table_entry_t table[64];
    size_t              n = 0U;

    if ((entries == NULL) || (count == 0U) || (buf == NULL) || (buf_size == 0U))
    {
        return SW_ERR_PARAM;
    }

    if (count > (sizeof(table) / sizeof(table[0])))
    {
        return SW_ERR_OVERFLOW;
    }

    for (size_t i = 0U; i < count; i++)
    {
        if (entries[i].base.get == NULL)
        {
            continue;
        }
        table[n++] = entries[i].base;
    }

    return point_table_to_json_ex(table, n, buf, buf_size, s_get_fail_policy, NULL);
}

sw_err_t cloud_point_to_json_filtered(const cloud_point_entry_t *entries, size_t count,
                                       const char *const *ids, size_t id_count,
                                       char *buf, size_t buf_size)
{
    point_table_entry_t table[64];
    size_t              n = 0U;

    if ((entries == NULL) || (count == 0U) || (ids == NULL) || (id_count == 0U))
    {
        return SW_ERR_PARAM;
    }

    if (count > (sizeof(table) / sizeof(table[0])))
    {
        return SW_ERR_OVERFLOW;
    }

    for (size_t i = 0U; i < count; i++)
    {
        if (entries[i].base.get == NULL)
        {
            continue;
        }
        table[n++] = entries[i].base;
    }

    return point_table_to_json_filtered(table, n, ids, id_count, buf, buf_size);
}
