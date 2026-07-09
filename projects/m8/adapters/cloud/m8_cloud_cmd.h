/**
 * @file    m8_cloud_cmd.h
 * @brief   M8 云端命令类点位 handler
 * @author  HUWANGWEI
 * @date    2026-07-09
 */

#ifndef PROJECTS_M8_ADAPTERS_CLOUD_M8_CLOUD_CMD_H
#define PROJECTS_M8_ADAPTERS_CLOUD_M8_CLOUD_CMD_H

#include "framework/common/point_table/point_table.h"
#include "framework/common/sw_error.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 设备命令 */
sw_err_t m8_cloud_set_cmd_safe_home(const point_value_t *in);
sw_err_t m8_cloud_set_cmd_stop(const point_value_t *in);

/* 持续型水路/风机/照明 */
sw_err_t m8_cloud_set_cmd_water_pre_rinse(const point_value_t *in);
sw_err_t m8_cloud_get_cmd_water_pre_rinse(point_value_t *out);
sw_err_t m8_cloud_set_cmd_water_shampoo(const point_value_t *in);
sw_err_t m8_cloud_get_cmd_water_shampoo(point_value_t *out);
sw_err_t m8_cloud_set_cmd_water_side_brush(const point_value_t *in);
sw_err_t m8_cloud_get_cmd_water_side_brush(point_value_t *out);
sw_err_t m8_cloud_set_cmd_water_foam_rinse(const point_value_t *in);
sw_err_t m8_cloud_get_cmd_water_foam_rinse(point_value_t *out);
sw_err_t m8_cloud_set_cmd_dryer_A(const point_value_t *in);
sw_err_t m8_cloud_get_cmd_dryer_A(point_value_t *out);
sw_err_t m8_cloud_set_cmd_floodlight(const point_value_t *in);
sw_err_t m8_cloud_get_cmd_floodlight(point_value_t *out);

/* 三态枚举机构命令 */
sw_err_t m8_cloud_set_cmd_top_brush_rotation(const point_value_t *in);
sw_err_t m8_cloud_get_cmd_top_brush_rotation(point_value_t *out);
sw_err_t m8_cloud_set_cmd_lifter_move(const point_value_t *in);
sw_err_t m8_cloud_get_cmd_lifter_move(point_value_t *out);
sw_err_t m8_cloud_set_cmd_side_brush_rotation(const point_value_t *in);
sw_err_t m8_cloud_get_cmd_side_brush_rotation(point_value_t *out);
sw_err_t m8_cloud_set_cmd_gantry_move(const point_value_t *in);
sw_err_t m8_cloud_get_cmd_gantry_move(point_value_t *out);
sw_err_t m8_cloud_set_cmd_wheel_lock(const point_value_t *in);
sw_err_t m8_cloud_get_cmd_wheel_lock(point_value_t *out);

/* 功能配置 */
sw_err_t m8_cloud_set_cmd_station_stop_func(const point_value_t *in);
sw_err_t m8_cloud_get_cmd_station_stop_func(point_value_t *out);
sw_err_t m8_cloud_set_cmd_no_air_drying(const point_value_t *in);
sw_err_t m8_cloud_get_cmd_no_air_drying(point_value_t *out);
sw_err_t m8_cloud_set_cmd_config_sensor_water(const point_value_t *in);
sw_err_t m8_cloud_get_cmd_config_sensor_water(point_value_t *out);
sw_err_t m8_cloud_set_cmd_config_sensor_fl_collision(const point_value_t *in);
sw_err_t m8_cloud_get_cmd_config_sensor_fl_collision(point_value_t *out);
sw_err_t m8_cloud_set_cmd_config_sensor_fr_collision(const point_value_t *in);
sw_err_t m8_cloud_get_cmd_config_sensor_fr_collision(point_value_t *out);
sw_err_t m8_cloud_set_cmd_config_sensor_high_limit(const point_value_t *in);
sw_err_t m8_cloud_get_cmd_config_sensor_high_limit(point_value_t *out);

/* 紧急强制动作 */
sw_err_t m8_cloud_set_cmd_gantry_force_move_backward(const point_value_t *in);
sw_err_t m8_cloud_set_cmd_gantry_force_move_forward(const point_value_t *in);
sw_err_t m8_cloud_set_cmd_lifter_force_down(const point_value_t *in);
sw_err_t m8_cloud_set_cmd_lifter_force_up(const point_value_t *in);

#ifdef __cplusplus
}
#endif

#endif /* PROJECTS_M8_ADAPTERS_CLOUD_M8_CLOUD_CMD_H */
