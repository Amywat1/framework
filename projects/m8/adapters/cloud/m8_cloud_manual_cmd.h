/**
 * @file    m8_cloud_manual_cmd.h
 * @brief   M8 云端手动机构动作点位 handler
 * @author  HUWANGWEI
 * @date    2026-07-08
 */

#ifndef PROJECTS_M8_ADAPTERS_CLOUD_M8_CLOUD_MANUAL_CMD_H
#define PROJECTS_M8_ADAPTERS_CLOUD_M8_CLOUD_MANUAL_CMD_H

#include "framework/common/point_table/point_table.h"
#include "framework/common/sw_error.h"

#ifdef __cplusplus
extern "C" {
#endif

sw_err_t m8_cloud_set_cmd_gantry_fwd(const point_value_t *in);
sw_err_t m8_cloud_set_cmd_gantry_rev(const point_value_t *in);
sw_err_t m8_cloud_set_cmd_gantry_stop(const point_value_t *in);
sw_err_t m8_cloud_set_cmd_brush_side(const point_value_t *in);
sw_err_t m8_cloud_set_cmd_brush_top(const point_value_t *in);

#ifdef __cplusplus
}
#endif

#endif /* PROJECTS_M8_ADAPTERS_CLOUD_M8_CLOUD_MANUAL_CMD_H */
