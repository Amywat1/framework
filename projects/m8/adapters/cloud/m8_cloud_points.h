/**
 * @file    m8_cloud_points.h
 * @brief   M8 云端物模型点位 X-macro 登记表
 * @author  HUWANGWEI
 * @date    2026-07-08
 *
 * @note    列：SEM ACCESS POLICY ID TYPE GET SET CMD SERVICE
 */

#ifndef PROJECTS_M8_ADAPTERS_CLOUD_M8_CLOUD_POINTS_H
#define PROJECTS_M8_ADAPTERS_CLOUD_M8_CLOUD_POINTS_H

#include "framework/cloud/cloud_point.h"
#include "framework/ports/inbound/command/command_port.h"
#include "projects/m8/adapters/cloud/m8_cloud_telemetry.h"
#include "projects/m8/adapters/cloud/m8_cloud_service.h"
#include "projects/m8/adapters/cloud/m8_cloud_manual_cmd.h"

#define M8_CLOUD_POINTS(X) \
    X(CLOUD_POINT_SEM_TELEMETRY,     CLOUD_POINT_ACCESS_RO, CLOUD_REPORT_PERIODIC,     "sts_stopping",           POINT_TYPE_BOOL,   m8_cloud_get_sts_stopping,           NULL, CMD_NONE,            NULL) \
    X(CLOUD_POINT_SEM_TELEMETRY,     CLOUD_POINT_ACCESS_RO, CLOUD_REPORT_PERIODIC,     "sts_standby",            POINT_TYPE_BOOL,   m8_cloud_get_sts_standby,            NULL, CMD_NONE,            NULL) \
    X(CLOUD_POINT_SEM_TELEMETRY,     CLOUD_POINT_ACCESS_RO, CLOUD_REPORT_ON_CHANGE,    "sts_dev_warning",        POINT_TYPE_BOOL,   m8_cloud_get_sts_dev_warning,        NULL, CMD_NONE,            NULL) \
    X(CLOUD_POINT_SEM_TELEMETRY,     CLOUD_POINT_ACCESS_RO, CLOUD_REPORT_ON_CHANGE,    "sts_emergency",          POINT_TYPE_BOOL,   m8_cloud_get_sts_emergency,          NULL, CMD_NONE,            NULL) \
    X(CLOUD_POINT_SEM_TELEMETRY,     CLOUD_POINT_ACCESS_RO, CLOUD_REPORT_PERIODIC,     "sts_gantry_back_limit",  POINT_TYPE_BOOL,   m8_cloud_get_sts_gantry_back_limit,  NULL, CMD_NONE,            NULL) \
    X(CLOUD_POINT_SEM_TELEMETRY,     CLOUD_POINT_ACCESS_RO, CLOUD_REPORT_PERIODIC,     "sts_gantry_front_limit", POINT_TYPE_BOOL,   m8_cloud_get_sts_gantry_front_limit, NULL, CMD_NONE,            NULL) \
    X(CLOUD_POINT_SEM_TELEMETRY,     CLOUD_POINT_ACCESS_RO, CLOUD_REPORT_PERIODIC,     "sts_lifter_up_limit",    POINT_TYPE_BOOL,   m8_cloud_get_sts_lifter_up_limit,    NULL, CMD_NONE,            NULL) \
    X(CLOUD_POINT_SEM_TELEMETRY,     CLOUD_POINT_ACCESS_RO, CLOUD_REPORT_PERIODIC,     "sts_lifter_down_limit",  POINT_TYPE_BOOL,   m8_cloud_get_sts_lifter_down_limit,  NULL, CMD_NONE,            NULL) \
    X(CLOUD_POINT_SEM_TELEMETRY,     CLOUD_POINT_ACCESS_RO, CLOUD_REPORT_PERIODIC,     "sts_wheel_lock_zero",    POINT_TYPE_BOOL,   m8_cloud_get_sts_wheel_lock_zero,    NULL, CMD_NONE,            NULL) \
    X(CLOUD_POINT_SEM_TELEMETRY,     CLOUD_POINT_ACCESS_RO, CLOUD_REPORT_ON_CHANGE,    "sts_gantry_position",    POINT_TYPE_INT,    m8_cloud_get_sts_gantry_position,    NULL, CMD_NONE,            NULL) \
    X(CLOUD_POINT_SEM_TELEMETRY,     CLOUD_POINT_ACCESS_RO, CLOUD_REPORT_RESYNC_ONLY,  "sts_firmware_version",   POINT_TYPE_STRING, m8_cloud_get_sts_firmware_version,   NULL, CMD_NONE,            NULL) \
    X(CLOUD_POINT_SEM_TELEMETRY,     CLOUD_POINT_ACCESS_RO, CLOUD_REPORT_RESYNC_ONLY,  "sts_device_model",       POINT_TYPE_STRING, m8_cloud_get_sts_device_model,       NULL, CMD_NONE,            NULL) \
    X(CLOUD_POINT_SEM_TELEMETRY,     CLOUD_POINT_ACCESS_RO, CLOUD_REPORT_PERIODIC,     "sts_communication_test", POINT_TYPE_BOOL,   m8_cloud_get_sts_communication_test, NULL, CMD_NONE,            NULL) \
    X(CLOUD_POINT_SEM_DEVICE_CMD,    CLOUD_POINT_ACCESS_WO, CLOUD_REPORT_NEVER,        "cmd_home",               POINT_TYPE_BOOL,   cloud_point_get_pulse_false,         NULL, CMD_HOME_DEVICE,     NULL) \
    X(CLOUD_POINT_SEM_DEVICE_CMD,    CLOUD_POINT_ACCESS_WO, CLOUD_REPORT_NEVER,        "cmd_custom_stop",        POINT_TYPE_BOOL,   cloud_point_get_pulse_false,         NULL, CMD_STOP_WASH,       NULL) \
    X(CLOUD_POINT_SEM_CLOUD_SERVICE, CLOUD_POINT_ACCESS_WO, CLOUD_REPORT_NEVER,        "cmd_sync",               POINT_TYPE_BOOL,   cloud_point_get_pulse_false,         NULL, CMD_NONE,            m8_cloud_service_sync) \
    X(CLOUD_POINT_SEM_CLOUD_SERVICE, CLOUD_POINT_ACCESS_WO, CLOUD_REPORT_NEVER,        "cmd_communication_test", POINT_TYPE_BOOL,   NULL,                                NULL, CMD_NONE,            m8_cloud_service_comm_test) \
    X(CLOUD_POINT_SEM_MANUAL_ACT,    CLOUD_POINT_ACCESS_WO, CLOUD_REPORT_NEVER,        "cmd_gantry_fwd",         POINT_TYPE_BOOL,   cloud_point_get_pulse_false,         m8_cloud_set_cmd_gantry_fwd,  CMD_NONE, NULL) \
    X(CLOUD_POINT_SEM_MANUAL_ACT,    CLOUD_POINT_ACCESS_WO, CLOUD_REPORT_NEVER,        "cmd_gantry_rev",         POINT_TYPE_BOOL,   cloud_point_get_pulse_false,         m8_cloud_set_cmd_gantry_rev,  CMD_NONE, NULL) \
    X(CLOUD_POINT_SEM_MANUAL_ACT,    CLOUD_POINT_ACCESS_WO, CLOUD_REPORT_NEVER,        "cmd_gantry_stop",        POINT_TYPE_BOOL,   cloud_point_get_pulse_false,         m8_cloud_set_cmd_gantry_stop, CMD_NONE, NULL) \
    X(CLOUD_POINT_SEM_MANUAL_ACT,    CLOUD_POINT_ACCESS_WO, CLOUD_REPORT_NEVER,        "cmd_brush_side",         POINT_TYPE_BOOL,   cloud_point_get_pulse_false,         m8_cloud_set_cmd_brush_side,  CMD_NONE, NULL) \
    X(CLOUD_POINT_SEM_MANUAL_ACT,    CLOUD_POINT_ACCESS_WO, CLOUD_REPORT_NEVER,        "cmd_brush_top",          POINT_TYPE_BOOL,   cloud_point_get_pulse_false,         m8_cloud_set_cmd_brush_top,   CMD_NONE, NULL)

#endif /* PROJECTS_M8_ADAPTERS_CLOUD_M8_CLOUD_POINTS_H */
