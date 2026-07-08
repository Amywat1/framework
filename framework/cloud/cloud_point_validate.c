/**
 * @file    cloud_point_validate.c
 * @brief   云端物模型点位表登记期校验实现
 * @author  HUWANGWEI
 * @date    2026-07-08
 */

#include "framework/cloud/cloud_point.h"
#include "framework/common/log.h"
#include <string.h>

static bool id_seen_before(const cloud_point_entry_t *entries,
                            size_t count,
                            size_t index)
{
    for (size_t i = 0U; i < index; i++)
    {
        if ((entries[i].base.id != NULL) &&
            (entries[index].base.id != NULL) &&
            (strcmp(entries[i].base.id, entries[index].base.id) == 0))
        {
            return true;
        }
    }
    return false;
}

static sw_err_t validate_one(const cloud_point_entry_t *entry)
{
    if ((entry->base.id == NULL) || (entry->base.id[0] == '\0'))
    {
        LOG_ERROR("cloud_point_validate: empty id");
        return SW_ERR_PARAM;
    }

    if (entry->access == CLOUD_POINT_ACCESS_RO)
    {
        if (entry->base.get == NULL)
        {
            LOG_ERROR("cloud_point_validate: RO id=%s missing get", entry->base.id);
            return SW_ERR_PARAM;
        }
        if (entry->base.set != NULL)
        {
            LOG_ERROR("cloud_point_validate: RO id=%s must not have set", entry->base.id);
            return SW_ERR_PARAM;
        }
    }

    if (entry->access == CLOUD_POINT_ACCESS_WO)
    {
        if ((entry->semantic == CLOUD_POINT_SEM_TELEMETRY) && (entry->base.set == NULL))
        {
            LOG_ERROR("cloud_point_validate: WO telemetry id=%s invalid", entry->base.id);
            return SW_ERR_PARAM;
        }
    }

    switch (entry->semantic)
    {
        case CLOUD_POINT_SEM_TELEMETRY:
            if (entry->base.get == NULL)
            {
                LOG_ERROR("cloud_point_validate: telemetry id=%s missing get", entry->base.id);
                return SW_ERR_PARAM;
            }
            break;

        case CLOUD_POINT_SEM_DEVICE_CMD:
            if (entry->cmd_type == CMD_NONE)
            {
                LOG_ERROR("cloud_point_validate: device_cmd id=%s missing cmd_type", entry->base.id);
                return SW_ERR_PARAM;
            }
            break;

        case CLOUD_POINT_SEM_CLOUD_SERVICE:
            if (entry->service == NULL)
            {
                LOG_ERROR("cloud_point_validate: cloud_service id=%s missing service", entry->base.id);
                return SW_ERR_PARAM;
            }
            break;

        case CLOUD_POINT_SEM_MANUAL_ACT:
            if (entry->base.set == NULL)
            {
                LOG_ERROR("cloud_point_validate: manual_act id=%s missing set", entry->base.id);
                return SW_ERR_PARAM;
            }
            break;

        default:
            LOG_ERROR("cloud_point_validate: id=%s unknown semantic", entry->base.id);
            return SW_ERR_PARAM;
    }

    return SW_OK;
}

sw_err_t cloud_point_validate(const cloud_point_entry_t *entries, size_t count)
{
    if ((entries == NULL) || (count == 0U))
    {
        LOG_ERROR("cloud_point_validate: empty model");
        return SW_ERR_PARAM;
    }

    for (size_t i = 0U; i < count; i++)
    {
        if (id_seen_before(entries, count, i))
        {
            LOG_ERROR("cloud_point_validate: duplicate id=%s", entries[i].base.id);
            return SW_ERR_PARAM;
        }

        if (validate_one(&entries[i]) != SW_OK)
        {
            return SW_ERR_PARAM;
        }
    }

    return SW_OK;
}
