/**
 * @file    cloud_point_dispatch.c
 * @brief   云端物模型点位按语义分派写入
 * @author  HUWANGWEI
 * @date    2026-07-08
 *
 * @note    本文件只处理"已解析的值该交给谁"这一语义规则：遥测点位拒绝写入、
 *          设备命令经回调提交、云服务调 service、手动动作调 set。
 *          JSON 编解码在 `adapters/outbound/cloud/cloud_point_json.c`。
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

sw_err_t cloud_point_apply_value(const cloud_point_entry_t *entry,
                                 const point_value_t       *val,
                                 point_apply_result_t      *result)
{
    sw_err_t ret = SW_OK;

    if ((entry == NULL) || (val == NULL)) {
        return SW_ERR_PARAM;
    }

    /* 只读点位在此拒绝：access 与 semantic 两个维度都可能声明只读 */
    if ((entry->access == CLOUD_POINT_ACCESS_RO) || (entry->semantic == CLOUD_POINT_SEM_TELEMETRY)) {
        LOG_WARN("cloud_point: id=%s is read-only", entry->base.id);
        point_apply_result_record_error(result, entry->base.id, SW_ERR_STATE);
        return SW_ERR_STATE;
    }

    switch (entry->semantic) {
    case CLOUD_POINT_SEM_DEVICE_CMD:
        /* 脉冲语义：只有置真才触发，置假是回落，不构成命令 */
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

    case CLOUD_POINT_SEM_TELEMETRY:
    default:
        /* TELEMETRY 已在上面的只读判定中拦下，此处不可达；保留分支使
         * 新增语义未处理时编译器给出 -Wswitch 提示。 */
        point_apply_result_record_error(result, entry->base.id, SW_ERR_PARAM);
        return SW_ERR_PARAM;
    }
}
