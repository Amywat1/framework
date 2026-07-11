/**
 * @file    m8_top_brush_lift.h
 * @brief   M8 顶刷升降机构接口
 * @author  HUWANGWEI
 * @date    2026-07-11
 */

#ifndef M8_DOMAIN_MECHANISM_TOP_BRUSH_LIFT_H
#define M8_DOMAIN_MECHANISM_TOP_BRUSH_LIFT_H

#include "framework/domain/device_control/patterns/motion_lifecycle.h"
#include "framework/ports/outbound/hal/motor/hal_motor_exec_port.h"
#include "framework/common/sw_error.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum
{
    LIFT_STATE_IDLE = 0,
    LIFT_STATE_LIFTING,
    LIFT_STATE_LOWERING,
    LIFT_STATE_STOPPING,
    LIFT_STATE_FAULT,
} lift_state_t;

sw_err_t lift_init(hal_motor_exec_t *exec, int motor, const motion_lifecycle_opts_t *opts);
sw_err_t lift_up(int speed_gear, const hal_motor_move_spec_t *spec);
sw_err_t lift_down(int speed_gear, const hal_motor_move_spec_t *spec);
sw_err_t lift_stop(void);
sw_err_t lift_home(void);
lift_state_t lift_state(void);
int64_t lift_position(void);
hal_motor_fault_code_t lift_fault_code(void);
sw_err_t lift_recover(hal_motor_recovery_step_t step);

#ifdef __cplusplus
}
#endif

#endif /* M8_DOMAIN_MECHANISM_TOP_BRUSH_LIFT_H */
