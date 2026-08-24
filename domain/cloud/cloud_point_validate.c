/**
 * @file    cloud_point_validate.c
 * @brief   云端物模型点位表登记期校验实现
 * @author  HUWANGWEI
 * @date    2026-07-08
 */

#include "common/log.h"
#include "domain/cloud/cloud_point.h"

#include <string.h>

static bool id_seen_before(const cloud_point_entry_t *entries, size_t index)
{
    for (size_t i = 0U; i < index; i++) {
        if ((entries[i].base.id != NULL) && (entries[index].base.id != NULL)
            && (strcmp(entries[i].base.id, entries[index].base.id) == 0)) {
            return true;
        }
    }
    return false;
}

static sw_err_t validate_one(const cloud_point_entry_t *entry)
{
    if ((entry->base.id == NULL) || (entry->base.id[0] == '\0')) {
        LOG_ERROR("cloud_point_validate: empty id");
        return SW_ERR_PARAM;
    }

    if (entry->on_change) {
        if (entry->base.get == NULL) {
            LOG_ERROR("cloud_point_validate: on_change id=%s missing get", entry->base.id);
            return SW_ERR_PARAM;
        }
        if (entry->base.type == POINT_TYPE_FLOAT) {
            LOG_ERROR("cloud_point_validate: on_change id=%s forbids FLOAT", entry->base.id);
            return SW_ERR_PARAM;
        }
    }

    switch (entry->kind) {
    case CLOUD_KIND_TELEMETRY:
        if (entry->base.get == NULL) {
            LOG_ERROR("cloud_point_validate: telemetry id=%s missing get", entry->base.id);
            return SW_ERR_PARAM;
        }
        if (entry->base.set != NULL) {
            LOG_ERROR("cloud_point_validate: telemetry id=%s must not have set", entry->base.id);
            return SW_ERR_PARAM;
        }
        if (entry->cmd_kind != DEV_CMD_NONE) {
            LOG_ERROR("cloud_point_validate: telemetry id=%s must not have cmd_kind", entry->base.id);
            return SW_ERR_PARAM;
        }
        break;

    case CLOUD_KIND_COMMAND:
        if (entry->base.type != POINT_TYPE_BOOL) {
            LOG_ERROR("cloud_point_validate: command id=%s must be bool", entry->base.id);
            return SW_ERR_PARAM;
        }
        if (entry->cmd_kind == DEV_CMD_NONE) {
            LOG_ERROR("cloud_point_validate: command id=%s missing cmd_kind", entry->base.id);
            return SW_ERR_PARAM;
        }
        break;

    case CLOUD_KIND_WRITE:
        if (entry->base.set == NULL) {
            LOG_ERROR("cloud_point_validate: write id=%s missing set", entry->base.id);
            return SW_ERR_PARAM;
        }
        if (entry->cmd_kind != DEV_CMD_NONE) {
            LOG_ERROR("cloud_point_validate: write id=%s must not have cmd_kind", entry->base.id);
            return SW_ERR_PARAM;
        }
        break;

    default:
        LOG_ERROR("cloud_point_validate: id=%s unknown kind", entry->base.id);
        return SW_ERR_PARAM;
    }

    return SW_OK;
}

sw_err_t cloud_point_validate(const cloud_point_entry_t *entries, size_t count)
{
    if ((entries == NULL) || (count == 0U) || (count > CLOUD_POINT_TABLE_MAX)) {
        LOG_ERROR("cloud_point_validate: empty or oversized model");
        return SW_ERR_PARAM;
    }

    for (size_t i = 0U; i < count; i++) {
        if (id_seen_before(entries, i)) {
            LOG_ERROR("cloud_point_validate: duplicate id=%s", entries[i].base.id);
            return SW_ERR_PARAM;
        }

        if (validate_one(&entries[i]) != SW_OK) {
            return SW_ERR_PARAM;
        }
    }

    return SW_OK;
}
