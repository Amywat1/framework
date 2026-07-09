/**
 * @file    m8_cloud_points.h
 * @brief   M8 云端物模型点位 X-macro 登记表（与 M8物模型点位.xlsx 对齐）
 * @author  HUWANGWEI
 * @date    2026-07-09
 *
 * @note    列：SEM ACCESS POLICY ID TYPE GET SET CMD SERVICE
 */

#ifndef PROJECTS_M8_ADAPTERS_CLOUD_M8_CLOUD_POINTS_H
#define PROJECTS_M8_ADAPTERS_CLOUD_M8_CLOUD_POINTS_H

#include "framework/cloud/cloud_point.h"
#include "framework/ports/inbound/command/command_port.h"
#include "projects/m8/adapters/cloud/m8_cloud_telemetry.h"
#include "projects/m8/adapters/cloud/m8_cloud_service.h"
#include "projects/m8/adapters/cloud/m8_cloud_cmd.h"

#define M8_CLOUD_POINTS(X) \
    X(CLOUD_POINT_SEM_TELEMETRY,     CLOUD_POINT_ACCESS_RO, CLOUD_REPORT_PERIODIC,     "sts_stopping",                       POINT_TYPE_BOOL,   m8_cloud_get_sts_stopping,                       NULL, CMD_NONE,            NULL) \
    X(CLOUD_POINT_SEM_TELEMETRY,     CLOUD_POINT_ACCESS_RO, CLOUD_REPORT_PERIODIC,     "sts_standby",                        POINT_TYPE_BOOL,   m8_cloud_get_sts_standby,                        NULL, CMD_NONE,            NULL) \
    X(CLOUD_POINT_SEM_TELEMETRY,     CLOUD_POINT_ACCESS_RO, CLOUD_REPORT_PERIODIC,     "sts_homing",                         POINT_TYPE_BOOL,   m8_cloud_get_sts_homing,                         NULL, CMD_NONE,            NULL) \
    X(CLOUD_POINT_SEM_TELEMETRY,     CLOUD_POINT_ACCESS_RO, CLOUD_REPORT_PERIODIC,     "sts_normal_homing",                  POINT_TYPE_BOOL,   m8_cloud_get_sts_normal_homing,                  NULL, CMD_NONE,            NULL) \
    X(CLOUD_POINT_SEM_TELEMETRY,     CLOUD_POINT_ACCESS_RO, CLOUD_REPORT_PERIODIC,     "sts_custom_stopping",                POINT_TYPE_BOOL,   m8_cloud_get_sts_custom_stopping,                NULL, CMD_NONE,            NULL) \
    X(CLOUD_POINT_SEM_TELEMETRY,     CLOUD_POINT_ACCESS_RO, CLOUD_REPORT_PERIODIC,     "sts_warn_homing",                    POINT_TYPE_BOOL,   m8_cloud_get_sts_warn_homing,                    NULL, CMD_NONE,            NULL) \
    X(CLOUD_POINT_SEM_TELEMETRY,     CLOUD_POINT_ACCESS_RO, CLOUD_REPORT_ON_CHANGE,    "sts_dev_warning",                    POINT_TYPE_BOOL,   m8_cloud_get_sts_dev_warning,                    NULL, CMD_NONE,            NULL) \
    X(CLOUD_POINT_SEM_DEVICE_CMD,    CLOUD_POINT_ACCESS_WO, CLOUD_REPORT_NEVER,        "cmd_home",                           POINT_TYPE_BOOL,   cloud_point_get_echo_idle,                       NULL, CMD_HOME_DEVICE,     NULL) \
    X(CLOUD_POINT_SEM_CLOUD_SERVICE, CLOUD_POINT_ACCESS_WO, CLOUD_REPORT_NEVER,        "cmd_safe_home",                      POINT_TYPE_BOOL,   cloud_point_get_echo_idle,                       NULL, CMD_NONE,            m8_cloud_set_cmd_safe_home) \
    X(CLOUD_POINT_SEM_CLOUD_SERVICE, CLOUD_POINT_ACCESS_WO, CLOUD_REPORT_NEVER,        "cmd_start_wash",                     POINT_TYPE_BOOL,   cloud_point_get_echo_idle,                       NULL, CMD_NONE,            m8_cloud_service_start_wash) \
    X(CLOUD_POINT_SEM_MANUAL_ACT,    CLOUD_POINT_ACCESS_WO, CLOUD_REPORT_NEVER,        "cmd_stop",                           POINT_TYPE_BOOL,   cloud_point_get_echo_idle,                       m8_cloud_set_cmd_stop, CMD_NONE,          NULL) \
    X(CLOUD_POINT_SEM_CLOUD_SERVICE, CLOUD_POINT_ACCESS_WO, CLOUD_REPORT_NEVER,        "cmd_custom_stop",                    POINT_TYPE_BOOL,   cloud_point_get_echo_idle,                       NULL, CMD_NONE,            m8_cloud_service_custom_stop) \
    X(CLOUD_POINT_SEM_CLOUD_SERVICE, CLOUD_POINT_ACCESS_WO, CLOUD_REPORT_NEVER,        "cmd_sync",                           POINT_TYPE_BOOL,   cloud_point_get_echo_idle,                       NULL, CMD_NONE,            m8_cloud_service_sync) \
    X(CLOUD_POINT_SEM_CLOUD_SERVICE, CLOUD_POINT_ACCESS_WO, CLOUD_REPORT_ON_CHANGE,    "cmd_open_data_minitor",              POINT_TYPE_BOOL,   m8_cloud_get_cmd_open_data_monitor,              NULL, CMD_NONE,            m8_cloud_service_open_data_monitor) \
    X(CLOUD_POINT_SEM_MANUAL_ACT,    CLOUD_POINT_ACCESS_WO, CLOUD_REPORT_ON_CHANGE,    "cmd_water_pre_rinse",                POINT_TYPE_BOOL,   m8_cloud_get_cmd_water_pre_rinse,                m8_cloud_set_cmd_water_pre_rinse, CMD_NONE, NULL) \
    X(CLOUD_POINT_SEM_MANUAL_ACT,    CLOUD_POINT_ACCESS_WO, CLOUD_REPORT_ON_CHANGE,    "cmd_water_shampoo",                  POINT_TYPE_BOOL,   m8_cloud_get_cmd_water_shampoo,                  m8_cloud_set_cmd_water_shampoo, CMD_NONE, NULL) \
    X(CLOUD_POINT_SEM_MANUAL_ACT,    CLOUD_POINT_ACCESS_WO, CLOUD_REPORT_ON_CHANGE,    "cmd_water_side_brush",               POINT_TYPE_BOOL,   m8_cloud_get_cmd_water_side_brush,               m8_cloud_set_cmd_water_side_brush, CMD_NONE, NULL) \
    X(CLOUD_POINT_SEM_MANUAL_ACT,    CLOUD_POINT_ACCESS_WO, CLOUD_REPORT_ON_CHANGE,    "cmd_water_foam_rinse",               POINT_TYPE_BOOL,   m8_cloud_get_cmd_water_foam_rinse,               m8_cloud_set_cmd_water_foam_rinse, CMD_NONE, NULL) \
    X(CLOUD_POINT_SEM_MANUAL_ACT,    CLOUD_POINT_ACCESS_WO, CLOUD_REPORT_ON_CHANGE,    "cmd_top_brush_rotation",             POINT_TYPE_INT,    m8_cloud_get_cmd_top_brush_rotation,             m8_cloud_set_cmd_top_brush_rotation, CMD_NONE, NULL) \
    X(CLOUD_POINT_SEM_MANUAL_ACT,    CLOUD_POINT_ACCESS_WO, CLOUD_REPORT_ON_CHANGE,    "cmd_lifter_move",                    POINT_TYPE_INT,    m8_cloud_get_cmd_lifter_move,                    m8_cloud_set_cmd_lifter_move, CMD_NONE, NULL) \
    X(CLOUD_POINT_SEM_MANUAL_ACT,    CLOUD_POINT_ACCESS_WO, CLOUD_REPORT_ON_CHANGE,    "cmd_side_brush_rotation",            POINT_TYPE_INT,    m8_cloud_get_cmd_side_brush_rotation,            m8_cloud_set_cmd_side_brush_rotation, CMD_NONE, NULL) \
    X(CLOUD_POINT_SEM_MANUAL_ACT,    CLOUD_POINT_ACCESS_WO, CLOUD_REPORT_ON_CHANGE,    "cmd_gantry_move",                    POINT_TYPE_INT,    m8_cloud_get_cmd_gantry_move,                    m8_cloud_set_cmd_gantry_move, CMD_NONE, NULL) \
    X(CLOUD_POINT_SEM_MANUAL_ACT,    CLOUD_POINT_ACCESS_WO, CLOUD_REPORT_ON_CHANGE,    "cmd_dryer_A",                        POINT_TYPE_BOOL,   m8_cloud_get_cmd_dryer_A,                        m8_cloud_set_cmd_dryer_A, CMD_NONE, NULL) \
    X(CLOUD_POINT_SEM_MANUAL_ACT,    CLOUD_POINT_ACCESS_WO, CLOUD_REPORT_ON_CHANGE,    "cmd_wheel_lock",                     POINT_TYPE_INT,    m8_cloud_get_cmd_wheel_lock,                     m8_cloud_set_cmd_wheel_lock, CMD_NONE, NULL) \
    X(CLOUD_POINT_SEM_MANUAL_ACT,    CLOUD_POINT_ACCESS_WO, CLOUD_REPORT_ON_CHANGE,    "cmd_floodlight",                     POINT_TYPE_BOOL,   m8_cloud_get_cmd_floodlight,                     m8_cloud_set_cmd_floodlight, CMD_NONE, NULL) \
    X(CLOUD_POINT_SEM_TELEMETRY,     CLOUD_POINT_ACCESS_RO, CLOUD_REPORT_ON_CHANGE,    "sts_park_state",                     POINT_TYPE_BOOL,   m8_cloud_get_sts_park_state,                     NULL, CMD_NONE,            NULL) \
    X(CLOUD_POINT_SEM_TELEMETRY,     CLOUD_POINT_ACCESS_RO, CLOUD_REPORT_ON_CHANGE,    "sts_gantry_position",                POINT_TYPE_INT,    m8_cloud_get_sts_gantry_position,                NULL, CMD_NONE,            NULL) \
    X(CLOUD_POINT_SEM_TELEMETRY,     CLOUD_POINT_ACCESS_RO, CLOUD_REPORT_ON_CHANGE,    "sts_lifter_position",                POINT_TYPE_INT,    m8_cloud_get_sts_lifter_position,                NULL, CMD_NONE,            NULL) \
    X(CLOUD_POINT_SEM_TELEMETRY,     CLOUD_POINT_ACCESS_RO, CLOUD_REPORT_PERIODIC,     "sts_detect_height",                  POINT_TYPE_INT,    m8_cloud_get_sts_detect_height,                  NULL, CMD_NONE,            NULL) \
    X(CLOUD_POINT_SEM_TELEMETRY,     CLOUD_POINT_ACCESS_RO, CLOUD_REPORT_RESYNC_ONLY,  "sts_firmware_version",               POINT_TYPE_STRING, m8_cloud_get_sts_firmware_version,               NULL, CMD_NONE,            NULL) \
    X(CLOUD_POINT_SEM_TELEMETRY,     CLOUD_POINT_ACCESS_RO, CLOUD_REPORT_RESYNC_ONLY,  "sts_device_model",                   POINT_TYPE_STRING, m8_cloud_get_sts_device_model,                   NULL, CMD_NONE,            NULL) \
    X(CLOUD_POINT_SEM_TELEMETRY,     CLOUD_POINT_ACCESS_RO, CLOUD_REPORT_PERIODIC,     "sts_wash_today_counts",              POINT_TYPE_INT,    m8_cloud_get_sts_wash_today_counts,              NULL, CMD_NONE,            NULL) \
    X(CLOUD_POINT_SEM_TELEMETRY,     CLOUD_POINT_ACCESS_RO, CLOUD_REPORT_PERIODIC,     "sts_wash_start_counts",              POINT_TYPE_INT,    m8_cloud_get_sts_wash_start_counts,              NULL, CMD_NONE,            NULL) \
    X(CLOUD_POINT_SEM_TELEMETRY,     CLOUD_POINT_ACCESS_RO, CLOUD_REPORT_PERIODIC,     "sts_wash_complete_counts",           POINT_TYPE_INT,    m8_cloud_get_sts_wash_complete_counts,           NULL, CMD_NONE,            NULL) \
    X(CLOUD_POINT_SEM_TELEMETRY,     CLOUD_POINT_ACCESS_RO, CLOUD_REPORT_PERIODIC,     "sts_wash_failed_counts",             POINT_TYPE_INT,    m8_cloud_get_sts_wash_failed_counts,             NULL, CMD_NONE,            NULL) \
    X(CLOUD_POINT_SEM_TELEMETRY,     CLOUD_POINT_ACCESS_RO, CLOUD_REPORT_RESYNC_ONLY,  "sts_port_number",                    POINT_TYPE_INT,    m8_cloud_get_sts_port_number,                    NULL, CMD_NONE,            NULL) \
    X(CLOUD_POINT_SEM_TELEMETRY,     CLOUD_POINT_ACCESS_RO, CLOUD_REPORT_ON_CHANGE,    "sts_emergency",                      POINT_TYPE_BOOL,   m8_cloud_get_sts_emergency,                      NULL, CMD_NONE,            NULL) \
    X(CLOUD_POINT_SEM_TELEMETRY,     CLOUD_POINT_ACCESS_RO, CLOUD_REPORT_PERIODIC,     "sts_gantry_back_limit",              POINT_TYPE_BOOL,   m8_cloud_get_sts_gantry_back_limit,              NULL, CMD_NONE,            NULL) \
    X(CLOUD_POINT_SEM_TELEMETRY,     CLOUD_POINT_ACCESS_RO, CLOUD_REPORT_PERIODIC,     "sts_gantry_front_limit",             POINT_TYPE_BOOL,   m8_cloud_get_sts_gantry_front_limit,             NULL, CMD_NONE,            NULL) \
    X(CLOUD_POINT_SEM_TELEMETRY,     CLOUD_POINT_ACCESS_RO, CLOUD_REPORT_PERIODIC,     "sts_lifter_up_limit",                POINT_TYPE_BOOL,   m8_cloud_get_sts_lifter_up_limit,                NULL, CMD_NONE,            NULL) \
    X(CLOUD_POINT_SEM_TELEMETRY,     CLOUD_POINT_ACCESS_RO, CLOUD_REPORT_PERIODIC,     "sts_lifter_down_limit",              POINT_TYPE_BOOL,   m8_cloud_get_sts_lifter_down_limit,              NULL, CMD_NONE,            NULL) \
    X(CLOUD_POINT_SEM_TELEMETRY,     CLOUD_POINT_ACCESS_RO, CLOUD_REPORT_PERIODIC,     "sts_signal_stop",                    POINT_TYPE_BOOL,   m8_cloud_get_sts_signal_stop,                    NULL, CMD_NONE,            NULL) \
    X(CLOUD_POINT_SEM_TELEMETRY,     CLOUD_POINT_ACCESS_RO, CLOUD_REPORT_PERIODIC,     "sts_wheel_lock_arrive",              POINT_TYPE_BOOL,   m8_cloud_get_sts_wheel_lock_arrive,              NULL, CMD_NONE,            NULL) \
    X(CLOUD_POINT_SEM_TELEMETRY,     CLOUD_POINT_ACCESS_RO, CLOUD_REPORT_PERIODIC,     "sts_wheel_lock_zero",                POINT_TYPE_BOOL,   m8_cloud_get_sts_wheel_lock_zero,                NULL, CMD_NONE,            NULL) \
    X(CLOUD_POINT_SEM_TELEMETRY,     CLOUD_POINT_ACCESS_RO, CLOUD_REPORT_PERIODIC,     "sts_water_press",                    POINT_TYPE_BOOL,   m8_cloud_get_sts_water_press,                    NULL, CMD_NONE,            NULL) \
    X(CLOUD_POINT_SEM_TELEMETRY,     CLOUD_POINT_ACCESS_RO, CLOUD_REPORT_PERIODIC,     "sts_top_brush_crl_limit",            POINT_TYPE_BOOL,   m8_cloud_get_sts_top_brush_crl_limit,            NULL, CMD_NONE,            NULL) \
    X(CLOUD_POINT_SEM_TELEMETRY,     CLOUD_POINT_ACCESS_RO, CLOUD_REPORT_PERIODIC,     "sts_top_brush_collision",            POINT_TYPE_BOOL,   m8_cloud_get_sts_top_brush_collision,            NULL, CMD_NONE,            NULL) \
    X(CLOUD_POINT_SEM_TELEMETRY,     CLOUD_POINT_ACCESS_RO, CLOUD_REPORT_PERIODIC,     "sts_signal_height",                  POINT_TYPE_BOOL,   m8_cloud_get_sts_signal_height,                  NULL, CMD_NONE,            NULL) \
    X(CLOUD_POINT_SEM_TELEMETRY,     CLOUD_POINT_ACCESS_RO, CLOUD_REPORT_PERIODIC,     "sts_left_person_collision_strip",    POINT_TYPE_BOOL,   m8_cloud_get_sts_left_person_collision_strip,    NULL, CMD_NONE,            NULL) \
    X(CLOUD_POINT_SEM_TELEMETRY,     CLOUD_POINT_ACCESS_RO, CLOUD_REPORT_PERIODIC,     "sts_right_person_collision_strip",   POINT_TYPE_BOOL,   m8_cloud_get_sts_right_person_collision_strip,   NULL, CMD_NONE,            NULL) \
    X(CLOUD_POINT_SEM_TELEMETRY,     CLOUD_POINT_ACCESS_RO, CLOUD_REPORT_PERIODIC,     "sts_front_left_collision",           POINT_TYPE_BOOL,   m8_cloud_get_sts_front_left_collision,         NULL, CMD_NONE,            NULL) \
    X(CLOUD_POINT_SEM_TELEMETRY,     CLOUD_POINT_ACCESS_RO, CLOUD_REPORT_PERIODIC,     "sts_front_right_collision",          POINT_TYPE_BOOL,   m8_cloud_get_sts_front_right_collision,          NULL, CMD_NONE,            NULL) \
    X(CLOUD_POINT_SEM_CLOUD_SERVICE, CLOUD_POINT_ACCESS_WO, CLOUD_REPORT_NEVER,        "cmd_communication_test",             POINT_TYPE_BOOL,   NULL,                                            NULL, CMD_NONE,            m8_cloud_service_comm_test) \
    X(CLOUD_POINT_SEM_TELEMETRY,     CLOUD_POINT_ACCESS_RO, CLOUD_REPORT_ON_CHANGE,    "sts_communication_test",             POINT_TYPE_BOOL,   m8_cloud_get_sts_communication_test,             NULL, CMD_NONE,            NULL) \
    X(CLOUD_POINT_SEM_MANUAL_ACT,    CLOUD_POINT_ACCESS_WO, CLOUD_REPORT_ON_CHANGE,    "cmd_station_stop_func",              POINT_TYPE_BOOL,   m8_cloud_get_cmd_station_stop_func,              m8_cloud_set_cmd_station_stop_func, CMD_NONE, NULL) \
    X(CLOUD_POINT_SEM_MANUAL_ACT,    CLOUD_POINT_ACCESS_WO, CLOUD_REPORT_ON_CHANGE,    "cmd_no_air_drying",                  POINT_TYPE_BOOL,   m8_cloud_get_cmd_no_air_drying,                  m8_cloud_set_cmd_no_air_drying, CMD_NONE, NULL) \
    X(CLOUD_POINT_SEM_MANUAL_ACT,    CLOUD_POINT_ACCESS_WO, CLOUD_REPORT_ON_CHANGE,    "cmd_config_sensor_water",            POINT_TYPE_BOOL,   m8_cloud_get_cmd_config_sensor_water,            m8_cloud_set_cmd_config_sensor_water, CMD_NONE, NULL) \
    X(CLOUD_POINT_SEM_MANUAL_ACT,    CLOUD_POINT_ACCESS_WO, CLOUD_REPORT_ON_CHANGE,    "cmd_config_sensor_fl_collision",     POINT_TYPE_BOOL,   m8_cloud_get_cmd_config_sensor_fl_collision,     m8_cloud_set_cmd_config_sensor_fl_collision, CMD_NONE, NULL) \
    X(CLOUD_POINT_SEM_MANUAL_ACT,    CLOUD_POINT_ACCESS_WO, CLOUD_REPORT_ON_CHANGE,    "cmd_config_sensor_fr_collision",     POINT_TYPE_BOOL,   m8_cloud_get_cmd_config_sensor_fr_collision,     m8_cloud_set_cmd_config_sensor_fr_collision, CMD_NONE, NULL) \
    X(CLOUD_POINT_SEM_MANUAL_ACT,    CLOUD_POINT_ACCESS_WO, CLOUD_REPORT_ON_CHANGE,    "cmd_config_sensor_high_limit",       POINT_TYPE_BOOL,   m8_cloud_get_cmd_config_sensor_high_limit,       m8_cloud_set_cmd_config_sensor_high_limit, CMD_NONE, NULL) \
    X(CLOUD_POINT_SEM_MANUAL_ACT,    CLOUD_POINT_ACCESS_WO, CLOUD_REPORT_NEVER,        "cmd_gantry_force_move_backward",     POINT_TYPE_BOOL,   cloud_point_get_echo_idle,                       m8_cloud_set_cmd_gantry_force_move_backward, CMD_NONE, NULL) \
    X(CLOUD_POINT_SEM_MANUAL_ACT,    CLOUD_POINT_ACCESS_WO, CLOUD_REPORT_NEVER,        "cmd_gantry_force_move_forward",      POINT_TYPE_BOOL,   cloud_point_get_echo_idle,                       m8_cloud_set_cmd_gantry_force_move_forward, CMD_NONE, NULL) \
    X(CLOUD_POINT_SEM_MANUAL_ACT,    CLOUD_POINT_ACCESS_WO, CLOUD_REPORT_NEVER,        "cmd_lifter_force_down",              POINT_TYPE_BOOL,   cloud_point_get_echo_idle,                       m8_cloud_set_cmd_lifter_force_down, CMD_NONE, NULL) \
    X(CLOUD_POINT_SEM_MANUAL_ACT,    CLOUD_POINT_ACCESS_WO, CLOUD_REPORT_NEVER,        "cmd_lifter_force_up",                POINT_TYPE_BOOL,   cloud_point_get_echo_idle,                       m8_cloud_set_cmd_lifter_force_up, CMD_NONE, NULL) \
    X(CLOUD_POINT_SEM_TELEMETRY,     CLOUD_POINT_ACCESS_RO, CLOUD_REPORT_ON_CHANGE,    "sts_I1_IO_change",                   POINT_TYPE_STRING, m8_cloud_get_sts_I1_IO_change,                   NULL, CMD_NONE,            NULL) \
    X(CLOUD_POINT_SEM_TELEMETRY,     CLOUD_POINT_ACCESS_RO, CLOUD_REPORT_ON_CHANGE,    "sts_I1_IO",                          POINT_TYPE_STRING, m8_cloud_get_sts_I1_IO,                          NULL, CMD_NONE,            NULL) \
    X(CLOUD_POINT_SEM_TELEMETRY,     CLOUD_POINT_ACCESS_RO, CLOUD_REPORT_ON_CHANGE,    "sts_O1_IO_change",                   POINT_TYPE_STRING, m8_cloud_get_sts_O1_IO_change,                   NULL, CMD_NONE,            NULL) \
    X(CLOUD_POINT_SEM_TELEMETRY,     CLOUD_POINT_ACCESS_RO, CLOUD_REPORT_ON_CHANGE,    "sts_O1_IO",                          POINT_TYPE_STRING, m8_cloud_get_sts_O1_IO,                          NULL, CMD_NONE,            NULL)

#endif /* PROJECTS_M8_ADAPTERS_CLOUD_M8_CLOUD_POINTS_H */
