/**
 * @file    cloud_point_dispatch.c
 * @brief   云端物模型点位按 kind 分派写入
 * @author  HUWANGWEI
 * @date    2026-07-08
 *
 * @note    本文件只处理"已解析的值该交给谁"：遥测拒绝写入、设备命令经回调提交、
 *          点位写入调 set。JSON 编解码在 `adapters/outbound/cloud/cloud_point_json.c`。
 */

#include "common/log.h"
#include "domain/cloud/cloud_point.h"

#include <stddef.h>

static cloud_device_cmd_submit_fn_t s_device_cmd_submit = NULL;

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
 * @brief  按点位表把写入值翻译为设备命令
 */
static sw_err_t build_device_cmd(const cloud_point_entry_t *entry, const point_value_t *val, dev_cmd_t *cmd)
{
    int32_t param;

    if (entry->cmd_has_param) {
        param = entry->cmd_param;
    } else if (entry->base.type == POINT_TYPE_INT) {
        param = val->i;
    } else {
        param = val->b ? 1 : 0;
    }

    switch (entry->cmd_kind) {
    case DEV_CMD_MANUAL_ACTUATOR:
        *cmd = dev_cmd_make_manual(entry->cmd_act_id, param);
        return SW_OK;
    case DEV_CMD_START_WASH:
        *cmd = dev_cmd_make_start_wash(entry->cmd_has_param ? (wash_mode_t)entry->cmd_param : (wash_mode_t)0);
        return SW_OK;
    case DEV_CMD_SET_SERVICE:
        *cmd = dev_cmd_make_set_service(entry->cmd_has_param ? (entry->cmd_param != 0) : val->b);
        return SW_OK;
    default:
        *cmd = dev_cmd_make_simple(entry->cmd_kind);
        return SW_OK;
    }
}

/**
 * @brief  经 cloud_device_cmd_submit 回调提交设备命令
 */
static sw_err_t submit_device_cmd(const dev_cmd_t *cmd)
{
    if (s_device_cmd_submit == NULL) {
        LOG_WARN("cloud_point: device_cmd submit handler not registered");
        return SW_ERR_NOT_INIT;
    }

    if ((cmd == NULL) || (cmd->body.kind == DEV_CMD_NONE)) {
        return SW_ERR_PARAM;
    }

    return s_device_cmd_submit(cmd);
}

sw_err_t cloud_point_apply_value(const cloud_point_entry_t *entry,
                                 const point_value_t       *val,
                                 point_apply_result_t      *result)
{
    sw_err_t ret = SW_OK;
    dev_cmd_t cmd;

    if ((entry == NULL) || (val == NULL)) {
        return SW_ERR_PARAM;
    }

    switch (entry->kind) {
    case CLOUD_KIND_TELEMETRY:
        LOG_WARN("cloud_point: id=%s is telemetry (read-only)", entry->base.id);
        point_apply_result_record_error(result, entry->base.id, SW_ERR_STATE);
        return SW_ERR_STATE;

    case CLOUD_KIND_DEV_CMD:
        /* 脉冲：置假是回落，不构成设备命令；保持量每次写入都提交 */
        if (cloud_point_is_pulse(entry) && (entry->base.type == POINT_TYPE_BOOL) && !val->b) {
            return SW_OK;
        }
        ret = build_device_cmd(entry, val, &cmd);
        if (ret != SW_OK) {
            point_apply_result_record_error(result, entry->base.id, ret);
            return ret;
        }
        ret = submit_device_cmd(&cmd);
        if (ret != SW_OK) {
            point_apply_result_record_error(result, entry->base.id, ret);
            return ret;
        }
        if (entry->base.set != NULL) {
            ret = entry->base.set(val);
            if (ret != SW_OK) {
                point_apply_result_record_error(result, entry->base.id, ret);
            }
        }
        return ret;

    case CLOUD_KIND_SET:
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
