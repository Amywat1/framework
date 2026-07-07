/**
 * @file    point_table_from_json.c
 * @brief   标识符点位表 JSON 反序列化实现
 * @author  HUWANGWEI
 * @date    2026-07-02
 */

#include "framework/common/point_table/point_table.h"
#include "framework/common/log.h"
#include "third_party/cJSON/cJSON.h"
#include <string.h>

static const point_table_entry_t *find_entry(const char *id,
                                              const point_table_entry_t *entries,
                                              size_t count)
{
    for (size_t i = 0U; i < count; i++)
    {
        if (strcmp(entries[i].id, id) == 0)
        {
            return &entries[i];
        }
    }
    return NULL;
}

static void dispatch_one(const point_table_entry_t *entry, const cJSON *item)
{
    point_value_t val;

    memset(&val, 0, sizeof(val));

    switch (entry->type)
    {
        case POINT_TYPE_BOOL:
            if (!cJSON_IsBool(item) && !cJSON_IsNumber(item))
            {
                LOG_WARN("point_table: id=%s expect bool", entry->id);
                return;
            }
            val.b = cJSON_IsTrue(item) || (cJSON_IsNumber(item) && (item->valuedouble != 0.0));
            break;

        case POINT_TYPE_INT:
            if (!cJSON_IsNumber(item))
            {
                LOG_WARN("point_table: id=%s expect number", entry->id);
                return;
            }
            val.i = (int32_t)item->valuedouble;
            break;

        case POINT_TYPE_STRING:
            if (!cJSON_IsString(item) || (item->valuestring == NULL))
            {
                LOG_WARN("point_table: id=%s expect string", entry->id);
                return;
            }
            strncpy(val.s, item->valuestring, sizeof(val.s) - 1U);
            break;

        default:
            return;
    }

    if (entry->set == NULL)
    {
        LOG_WARN("point_table: id=%s is read-only", entry->id);
        return;
    }

    if (entry->set(&val) != SW_OK)
    {
        LOG_WARN("point_table: id=%s set failed", entry->id);
    }
}

void point_table_from_json(const point_table_entry_t *entries, size_t count,
                            const char *json_str)
{
    cJSON *root;
    cJSON *item;

    if (json_str == NULL)
    {
        return;
    }

    root = cJSON_Parse(json_str);
    if (root == NULL)
    {
        LOG_WARN("point_table: invalid json: %.80s", json_str);
        return;
    }

    cJSON_ArrayForEach(item, root)
    {
        const point_table_entry_t *entry;

        if (item->string == NULL)
        {
            continue;
        }

        entry = find_entry(item->string, entries, count);
        if (entry == NULL)
        {
            LOG_WARN("point_table: unknown id=%s", item->string);
            continue;
        }

        dispatch_one(entry, item);
    }

    cJSON_Delete(root);
}
