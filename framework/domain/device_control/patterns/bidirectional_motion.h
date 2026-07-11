/**
 * @file    bidirectional_motion.h
 * @brief   双向往复运动模式（单电机、正/反两方向）
 * @author  HUWANGWEI
 * @date    2026-07-11
 */

#ifndef DOMAIN_DEVICE_CONTROL_PATTERNS_BIDIRECTIONAL_MOTION_H
#define DOMAIN_DEVICE_CONTROL_PATTERNS_BIDIRECTIONAL_MOTION_H

#ifdef __cplusplus
extern "C" {
#endif

#include "framework/domain/device_control/patterns/motion_lifecycle.h"
#include "framework/ports/outbound/hal/motor/hal_motor_exec_port.h"
#include "framework/common/sw_error.h"
#include <stdbool.h>
#include <stdint.h>

typedef enum
{
    BIDIR_MOTION_STATE_IDLE = 0,
    BIDIR_MOTION_STATE_MOVING,
    BIDIR_MOTION_STATE_STOPPING,
    BIDIR_MOTION_STATE_FAULT,
} bidir_motion_state_t;

typedef struct
{
    hal_motor_exec_t         *exec;
    int                       motor;
    motion_lifecycle_opts_t   opts;
    bool                      inited;
} bidirectional_motion_t;

sw_err_t bidirectional_motion_init(bidirectional_motion_t *self,
                                   hal_motor_exec_t *exec,
                                   int motor,
                                   const motion_lifecycle_opts_t *opts);

sw_err_t bidirectional_motion_run(bidirectional_motion_t *self,
                                  hal_motor_dir_t dir,
                                  int speed_gear,
                                  const hal_motor_move_spec_t *spec);

sw_err_t bidirectional_motion_stop(bidirectional_motion_t *self);
sw_err_t bidirectional_motion_home(bidirectional_motion_t *self);
bidir_motion_state_t bidirectional_motion_state(const bidirectional_motion_t *self);
hal_motor_dir_t bidirectional_motion_direction(const bidirectional_motion_t *self);
int64_t bidirectional_motion_position(const bidirectional_motion_t *self);
hal_motor_fault_code_t bidirectional_motion_fault_code(const bidirectional_motion_t *self);
sw_err_t bidirectional_motion_recover(bidirectional_motion_t *self, hal_motor_recovery_step_t step);

#ifdef __cplusplus
}
#endif

#endif /* DOMAIN_DEVICE_CONTROL_PATTERNS_BIDIRECTIONAL_MOTION_H */
