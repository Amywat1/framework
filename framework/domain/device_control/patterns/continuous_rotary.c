/**
 * @file    continuous_rotary.c
 * @brief   单方向连续旋转运动模式实现
 * @author  HUWANGWEI
 * @date    2026-07-11
 */

#include "framework/domain/device_control/patterns/continuous_rotary.h"
#include <stddef.h>

static void rotary_publish_motion_completed(const continuous_rotary_t *self)
{
    if ((self != NULL) && (self->opts.motion_actuator_id != 0U))
    {
        actuator_publish_motion_completed(self->opts.motion_actuator_id);
    }
}

static continuous_rotary_state_t rotary_phase_to_state(const continuous_rotary_t *self)
{
    hal_motor_phase_t ph;

    if ((self == NULL) || !self->inited)
    {
        return CONTINUOUS_ROTARY_STATE_IDLE;
    }

    ph = hal_motor_phase(self->exec, self->motor);
    switch (ph)
    {
    case HAL_MOTOR_PHASE_STOPPED:
    case HAL_MOTOR_PHASE_WAITING_START:
    case HAL_MOTOR_PHASE_PAUSED:
        return CONTINUOUS_ROTARY_STATE_IDLE;

    case HAL_MOTOR_PHASE_RUNNING:
        return CONTINUOUS_ROTARY_STATE_RUNNING;

    case HAL_MOTOR_PHASE_DECELERATING:
    case HAL_MOTOR_PHASE_REVERSAL_WAIT:
        return CONTINUOUS_ROTARY_STATE_STOPPING;

    case HAL_MOTOR_PHASE_FAULT:
    case HAL_MOTOR_PHASE_ESTOP:
        return CONTINUOUS_ROTARY_STATE_FAULT;

    default:
        return CONTINUOUS_ROTARY_STATE_IDLE;
    }
}

sw_err_t continuous_rotary_init(continuous_rotary_t *self,
                                hal_motor_exec_t *exec,
                                int motor,
                                const motion_lifecycle_opts_t *opts)
{
    if ((self == NULL) || (exec == NULL))
    {
        return SW_ERR_PARAM;
    }

    self->exec   = exec;
    self->motor  = motor;
    self->opts   = (opts != NULL) ? *opts : (motion_lifecycle_opts_t){ 0 };
    self->inited = true;
    return SW_OK;
}

sw_err_t continuous_rotary_start(continuous_rotary_t *self, int speed_gear)
{
    hal_motor_cmd_result_t r;

    if ((self == NULL) || !self->inited)
    {
        return SW_ERR_NOT_INIT;
    }
    if (continuous_rotary_state(self) == CONTINUOUS_ROTARY_STATE_FAULT)
    {
        return SW_ERR_STATE;
    }

    r = hal_motor_run_continuous(self->exec, self->motor, hal_motor_speed_gear(speed_gear),
                                 HAL_MOTOR_DIR_FORWARD);
    return hal_motor_cmd_ok(r) ? SW_OK : SW_ERR_STATE;
}

sw_err_t continuous_rotary_stop(continuous_rotary_t *self)
{
    hal_motor_cmd_result_t r;

    if ((self == NULL) || !self->inited)
    {
        return SW_ERR_NOT_INIT;
    }

    r = hal_motor_stop(self->exec, self->motor);
    if (hal_motor_cmd_ok(r))
    {
        rotary_publish_motion_completed(self);
    }
    return SW_OK;
}

continuous_rotary_state_t continuous_rotary_state(const continuous_rotary_t *self)
{
    return rotary_phase_to_state(self);
}

hal_motor_fault_code_t continuous_rotary_fault_code(const continuous_rotary_t *self)
{
    if ((self == NULL) || !self->inited)
    {
        return HAL_MOTOR_FAULT_NONE;
    }
    return hal_motor_fault_code(self->exec, self->motor);
}

sw_err_t continuous_rotary_recover(continuous_rotary_t *self, hal_motor_recovery_step_t step)
{
    hal_motor_cmd_result_t r;

    if ((self == NULL) || !self->inited)
    {
        return SW_ERR_NOT_INIT;
    }

    r = hal_motor_recover(self->exec, self->motor, step);
    if (hal_motor_cmd_ok(r))
    {
        rotary_publish_motion_completed(self);
    }
    return SW_OK;
}
