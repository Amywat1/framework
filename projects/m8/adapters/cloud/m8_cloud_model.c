/**
 * @file    m8_cloud_model.c
 * @brief   M8 云端物模型点位表组装
 * @author  HUWANGWEI
 * @date    2026-07-08
 *
 * @note    新增/删除点位：在对应域 handler 文件实现 get/set，在本表登记一行。
 */

#include "projects/m8/adapters/cloud/m8_cloud_model.h"
#include "projects/m8/adapters/cloud/m8_cloud_common.h"
#include "projects/m8/adapters/cloud/m8_cloud_telemetry.h"
#include "projects/m8/adapters/cloud/m8_cloud_device_cmd.h"
#include "projects/m8/adapters/cloud/m8_cloud_service.h"
#include "projects/m8/adapters/cloud/m8_cloud_manual_cmd.h"

static const point_table_entry_t s_m8_cloud_model[] = {
    /* 只读遥测 */
    { "sts_stopping",           POINT_TYPE_BOOL,   m8_cloud_get_sts_stopping,           NULL },
    { "sts_standby",            POINT_TYPE_BOOL,   m8_cloud_get_sts_standby,            NULL },
    { "sts_dev_warning",        POINT_TYPE_BOOL,   m8_cloud_get_sts_dev_warning,        NULL },
    { "sts_emergency",          POINT_TYPE_BOOL,   m8_cloud_get_sts_emergency,          NULL },
    { "sts_gantry_back_limit",  POINT_TYPE_BOOL,   m8_cloud_get_sts_gantry_back_limit,  NULL },
    { "sts_gantry_front_limit", POINT_TYPE_BOOL,   m8_cloud_get_sts_gantry_front_limit, NULL },
    { "sts_lifter_up_limit",    POINT_TYPE_BOOL,   m8_cloud_get_sts_lifter_up_limit,    NULL },
    { "sts_lifter_down_limit",  POINT_TYPE_BOOL,   m8_cloud_get_sts_lifter_down_limit,  NULL },
    { "sts_wheel_lock_zero",    POINT_TYPE_BOOL,   m8_cloud_get_sts_wheel_lock_zero,    NULL },
    { "sts_gantry_position",    POINT_TYPE_INT,    m8_cloud_get_sts_gantry_position,    NULL },
    { "sts_firmware_version",   POINT_TYPE_STRING, m8_cloud_get_sts_firmware_version,   NULL },
    { "sts_device_model",       POINT_TYPE_STRING, m8_cloud_get_sts_device_model,       NULL },
    { "sts_communication_test", POINT_TYPE_BOOL,   m8_cloud_get_sts_communication_test, NULL },

    /* 设备生命周期命令 → command_port */
    { "cmd_home",               POINT_TYPE_BOOL,   m8_cloud_get_cmd_pulse,              m8_cloud_set_cmd_home },
    { "cmd_custom_stop",        POINT_TYPE_BOOL,   m8_cloud_get_cmd_pulse,              m8_cloud_set_cmd_custom_stop },

    /* 云端服务 */
    { "cmd_sync",               POINT_TYPE_BOOL,   m8_cloud_get_cmd_pulse,              m8_cloud_set_cmd_sync },
    { "cmd_communication_test", POINT_TYPE_BOOL,   NULL,                                m8_cloud_set_cmd_communication_test },

    /* 手动机构动作 → m8_manual_action */
    { "cmd_gantry_fwd",         POINT_TYPE_BOOL,   m8_cloud_get_cmd_pulse,              m8_cloud_set_cmd_gantry_fwd },
    { "cmd_gantry_rev",         POINT_TYPE_BOOL,   m8_cloud_get_cmd_pulse,              m8_cloud_set_cmd_gantry_rev },
    { "cmd_gantry_stop",        POINT_TYPE_BOOL,   m8_cloud_get_cmd_pulse,              m8_cloud_set_cmd_gantry_stop },
    { "cmd_brush_side",         POINT_TYPE_BOOL,   m8_cloud_get_cmd_pulse,              m8_cloud_set_cmd_brush_side },
    { "cmd_brush_top",          POINT_TYPE_BOOL,   m8_cloud_get_cmd_pulse,              m8_cloud_set_cmd_brush_top },
};

const point_table_entry_t *m8_cloud_model(size_t *out_count)
{
    if (out_count != NULL)
    {
        *out_count = sizeof(s_m8_cloud_model) / sizeof(s_m8_cloud_model[0]);
    }
    return s_m8_cloud_model;
}
