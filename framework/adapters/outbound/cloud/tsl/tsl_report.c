/**
 * @file    tsl_report.c
 * @brief   物模型点位表通用上行序列化实现
 * @author  HUWANGWEI
 * @date    2026-07-02
 */

#include "framework/adapters/outbound/cloud/tsl/tsl_point.h"
#include "third_party/cJSON/cJSON.h"
#include <stdlib.h>
#include <string.h>

sw_err_t tsl_build_report_json(const tsl_point_t *points, size_t count,
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
        const tsl_point_t *pt = &points[i];
        tsl_value_t        val;

        if ((pt->get == NULL) || (pt->get(&val) != SW_OK))
        {
            continue;
        }

        switch (pt->type)
        {
            case TSL_BOOL:
                cJSON_AddBoolToObject(root, pt->id, val.b);
                break;
            case TSL_INT:
                cJSON_AddNumberToObject(root, pt->id, (double)val.i);
                break;
            case TSL_STRING:
                cJSON_AddStringToObject(root, pt->id, val.s);
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
