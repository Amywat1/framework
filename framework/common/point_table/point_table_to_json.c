/**
 * @file    point_table_to_json.c
 * @brief   标识符点位表 JSON 序列化实现
 * @author  HUWANGWEI
 * @date    2026-07-02
 */

#include "framework/common/point_table/point_table.h"
#include "third_party/cJSON/cJSON.h"
#include <stdlib.h>
#include <string.h>

sw_err_t point_table_to_json(const point_table_entry_t *entries, size_t count,
                              char *buf, size_t buf_size)
{
    cJSON    *root = cJSON_CreateObject();
    sw_err_t  ret  = SW_ERR_PARAM;

    if (root == NULL)
    {
        return SW_ERR_PARAM;
    }

    for (size_t i = 0U; i < count; i++)
    {
        const point_table_entry_t *entry = &entries[i];
        point_value_t              val;

        if ((entry->get == NULL) || (entry->get(&val) != SW_OK))
        {
            continue;
        }

        switch (entry->type)
        {
            case POINT_TYPE_BOOL:
                cJSON_AddBoolToObject(root, entry->id, val.b);
                break;
            case POINT_TYPE_INT:
                cJSON_AddNumberToObject(root, entry->id, (double)val.i);
                break;
            case POINT_TYPE_STRING:
                cJSON_AddStringToObject(root, entry->id, val.s);
                break;
            default:
                break;
        }
    }

    {
        char *json = cJSON_PrintUnformatted(root);

        if (json != NULL)
        {
            size_t len = strlen(json);

            if (len < buf_size)
            {
                memcpy(buf, json, len + 1U);
                ret = SW_OK;
            }
            free(json);
        }
    }

    cJSON_Delete(root);
    return ret;
}
