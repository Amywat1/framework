/**
 * @file    continuous_rotary.h
 * @brief   单方向连续旋转运动模式
 * @author  HUWANGWEI
 * @date    2026-07-11
 */

#ifndef DOMAIN_DEVICE_CONTROL_PATTERNS_CONTINUOUS_ROTARY_H
#define DOMAIN_DEVICE_CONTROL_PATTERNS_CONTINUOUS_ROTARY_H

#ifdef __cplusplus
extern "C" {
#endif

#include "framework/domain/device_control/patterns/motion_lifecycle.h"
#include "framework/ports/outbound/hal/motor/hal_motor_exec_port.h"
#include "framework/common/sw_error.h"
#include <stdbool.h>

typedef enum
{
    CONTINUOUS_ROTARY_STATE_IDLE = 0,
    CONTINUOUS_ROTARY_STATE_RUNNING,
    CONTINUOUS_ROTARY_STATE_STOPPING,
    CONTINUOUS_ROTARY_STATE_FAULT,
} continuous_rotary_state_t;

typedef struct
{
    hal_motor_exec_t         *exec;
    int                       motor;
    motion_lifecycle_opts_t   opts;
    bool                      inited;
} continuous_rotary_t;

sw_err_t continuous_rotary_init(continuous_rotary_t *self,
                                hal_motor_exec_t *exec,
                                int motor,
                                const motion_lifecycle_opts_t *opts);

sw_err_t continuous_rotary_start(continuous_rotary_t *self, int speed_gear);
sw_err_t continuous_rotary_stop(continuous_rotary_t *self);
continuous_rotary_state_t continuous_rotary_state(const continuous_rotary_t *self);
hal_motor_fault_code_t continuous_rotary_fault_code(const continuous_rotary_t *self);
sw_err_t continuous_rotary_recover(continuous_rotary_t *self, hal_motor_recovery_step_t step);

#ifdef __cplusplus
}
#endif

#endif /* DOMAIN_DEVICE_CONTROL_PATTERNS_CONTINUOUS_ROTARY_H */
