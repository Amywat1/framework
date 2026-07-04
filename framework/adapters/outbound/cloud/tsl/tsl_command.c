/**
 * @file    tsl_command.c
 * @brief   物模型点位表通用下行分发实现
 * @author  HUWANGWEI
 * @date    2026-07-02
 */

#include "framework/adapters/outbound/cloud/tsl/tsl_point.h"
#include "framework/common/log.h"
#include "third_party/cJSON/cJSON.h"
#include <string.h>

static const tsl_point_t *find_point(const char *id, const tsl_point_t *points, size_t count)
{
    for (size_t i = 0U; i < count; i++)
    {
        if (strcmp(points[i].id, id) == 0)
        {
            return &points[i];
        }
    }
    return NULL;
}

static void dispatch_one(const tsl_point_t *pt, const cJSON *item)
{
    tsl_value_t val;

    memset(&val, 0, sizeof(val));

    switch (pt->type)
    {
        case TSL_BOOL:
            if (!cJSON_IsBool(item) && !cJSON_IsNumber(item))
            {
                LOG_WARN("tsl: id=%s expect bool", pt->id);
                return;
            }
            val.b = cJSON_IsTrue(item) || (cJSON_IsNumber(item) && (item->valuedouble != 0.0));
            break;

        case TSL_INT:
            if (!cJSON_IsNumber(item))
            {
                LOG_WARN("tsl: id=%s expect number", pt->id);
                return;
            }
            val.i = (int32_t)item->valuedouble;
            break;

        case TSL_STRING:
            if (!cJSON_IsString(item) || (item->valuestring == NULL))
            {
                LOG_WARN("tsl: id=%s expect string", pt->id);
                return;
            }
            strncpy(val.s, item->valuestring, sizeof(val.s) - 1U);
            break;

        default:
            return;
    }

    if (pt->set == NULL)
    {
        LOG_WARN("tsl: id=%s is read-only", pt->id);
        return;
    }

    if (pt->set(&val) != SW_OK)
    {
        LOG_WARN("tsl: id=%s set failed", pt->id);
    }
}

void tsl_command_dispatch(const tsl_point_t *points, size_t count, const char *json_str)
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
        LOG_WARN("tsl: invalid json: %.80s", json_str);
        return;
    }

    cJSON_ArrayForEach(item, root)
    {
        const tsl_point_t *pt;

        if (item->string == NULL)
        {
            continue;
        }

        pt = find_point(item->string, points, count);
        if (pt == NULL)
        {
            LOG_WARN("tsl: unknown id=%s", item->string);
            continue;
        }

        dispatch_one(pt, item);
    }

    cJSON_Delete(root);
}
