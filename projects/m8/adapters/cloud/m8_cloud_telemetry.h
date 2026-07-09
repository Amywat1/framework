/**
 * @file    m8_cloud_telemetry.h
 * @brief   M8 云端只读遥测点位 handler
 * @author  HUWANGWEI
 * @date    2026-07-09
 */

#ifndef PROJECTS_M8_ADAPTERS_CLOUD_M8_CLOUD_TELEMETRY_H
#define PROJECTS_M8_ADAPTERS_CLOUD_M8_CLOUD_TELEMETRY_H

#include "framework/common/point_table/point_table.h"
#include "framework/common/sw_error.h"

#ifdef __cplusplus
extern "C" {
#endif

sw_err_t m8_cloud_get_sts_stopping(point_value_t *out);
sw_err_t m8_cloud_get_sts_standby(point_value_t *out);
sw_err_t m8_cloud_get_sts_homing(point_value_t *out);
sw_err_t m8_cloud_get_sts_normal_homing(point_value_t *out);
sw_err_t m8_cloud_get_sts_custom_stopping(point_value_t *out);
sw_err_t m8_cloud_get_sts_warn_homing(point_value_t *out);
sw_err_t m8_cloud_get_sts_dev_warning(point_value_t *out);
sw_err_t m8_cloud_get_sts_park_state(point_value_t *out);
sw_err_t m8_cloud_get_sts_gantry_position(point_value_t *out);
sw_err_t m8_cloud_get_sts_lifter_position(point_value_t *out);
sw_err_t m8_cloud_get_sts_detect_height(point_value_t *out);
sw_err_t m8_cloud_get_sts_firmware_version(point_value_t *out);
sw_err_t m8_cloud_get_sts_device_model(point_value_t *out);
sw_err_t m8_cloud_get_sts_wash_today_counts(point_value_t *out);
sw_err_t m8_cloud_get_sts_wash_start_counts(point_value_t *out);
sw_err_t m8_cloud_get_sts_wash_complete_counts(point_value_t *out);
sw_err_t m8_cloud_get_sts_wash_failed_counts(point_value_t *out);
sw_err_t m8_cloud_get_sts_port_number(point_value_t *out);
sw_err_t m8_cloud_get_sts_emergency(point_value_t *out);
sw_err_t m8_cloud_get_sts_gantry_back_limit(point_value_t *out);
sw_err_t m8_cloud_get_sts_gantry_front_limit(point_value_t *out);
sw_err_t m8_cloud_get_sts_lifter_up_limit(point_value_t *out);
sw_err_t m8_cloud_get_sts_lifter_down_limit(point_value_t *out);
sw_err_t m8_cloud_get_sts_signal_stop(point_value_t *out);
sw_err_t m8_cloud_get_sts_wheel_lock_arrive(point_value_t *out);
sw_err_t m8_cloud_get_sts_wheel_lock_zero(point_value_t *out);
sw_err_t m8_cloud_get_sts_water_press(point_value_t *out);
sw_err_t m8_cloud_get_sts_top_brush_crl_limit(point_value_t *out);
sw_err_t m8_cloud_get_sts_top_brush_collision(point_value_t *out);
sw_err_t m8_cloud_get_sts_signal_height(point_value_t *out);
sw_err_t m8_cloud_get_sts_left_person_collision_strip(point_value_t *out);
sw_err_t m8_cloud_get_sts_right_person_collision_strip(point_value_t *out);
sw_err_t m8_cloud_get_sts_front_left_collision(point_value_t *out);
sw_err_t m8_cloud_get_sts_front_right_collision(point_value_t *out);
sw_err_t m8_cloud_get_sts_I1_IO_change(point_value_t *out);
sw_err_t m8_cloud_get_sts_I1_IO(point_value_t *out);
sw_err_t m8_cloud_get_sts_O1_IO_change(point_value_t *out);
sw_err_t m8_cloud_get_sts_O1_IO(point_value_t *out);

#ifdef __cplusplus
}
#endif

#endif /* PROJECTS_M8_ADAPTERS_CLOUD_M8_CLOUD_TELEMETRY_H */
