/**
 * @file    m8_manual_action.h
 * @brief   M8 手动机构动作统一入口
 * @author  HUWANGWEI
 * @date    2026-07-08
 *
 * @note    云端点位、CLI、仿真控制台共用本模块；运动类动作经 command_gateway 校验。
 */

#ifndef PROJECTS_M8_ADAPTERS_MANUAL_M8_MANUAL_ACTION_H
#define PROJECTS_M8_ADAPTERS_MANUAL_M8_MANUAL_ACTION_H

#include "projects/m8/domain/mechanism/m8_brush_rotation.h"
#include "framework/common/sw_error.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief  默认手动点动速度挡位 */
#define M8_MANUAL_DEFAULT_SPEED_GEAR   1

sw_err_t m8_manual_gantry_fwd(int speed_gear);
sw_err_t m8_manual_gantry_rev(int speed_gear);
sw_err_t m8_manual_gantry_stop(void);

sw_err_t m8_manual_brush_start(brush_id_t id, int speed_gear);
sw_err_t m8_manual_brush_stop(brush_id_t id);

#ifdef __cplusplus
}
#endif

#endif /* PROJECTS_M8_ADAPTERS_MANUAL_M8_MANUAL_ACTION_H */
