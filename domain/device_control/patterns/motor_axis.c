/**
 * @file    motor_axis.c
 * @brief   单轴运动模式实现
 * @author  HUWANGWEI
 * @date    2026-07-17
 */

#include "domain/device_control/patterns/motor_axis.h"

#include <stddef.h>

static motor_axis_state_t axis_phase_to_state(const motor_axis_t *self)
{
    hal_motor_phase_t ph;

    if ((self == NULL) || !self->inited) {
        return MOTOR_AXIS_STATE_IDLE;
    }

    ph = hal_motor_phase(self->exec, self->motor);
    switch (ph) {
    case HAL_MOTOR_PHASE_STOPPED:
    case HAL_MOTOR_PHASE_WAITING_START:
    case HAL_MOTOR_PHASE_PAUSED:
        return MOTOR_AXIS_STATE_IDLE;

    case HAL_MOTOR_PHASE_RUNNING:
        return MOTOR_AXIS_STATE_MOVING;

    case HAL_MOTOR_PHASE_DECELERATING:
    case HAL_MOTOR_PHASE_REVERSAL_WAIT:
        return MOTOR_AXIS_STATE_STOPPING;

    case HAL_MOTOR_PHASE_FAULT:
    case HAL_MOTOR_PHASE_ESTOP:
        return MOTOR_AXIS_STATE_FAULT;

    default:
        return MOTOR_AXIS_STATE_IDLE;
    }
}

sw_err_t motor_axis_init(motor_axis_t *self, hal_motor_exec_t *exec, int motor, const motion_lifecycle_opts_t *opts)
{
    if ((self == NULL) || (exec == NULL)) {
        return SW_ERR_PARAM;
    }

    self->exec   = exec;
    self->motor  = motor;
    self->opts   = (opts != NULL) ? *opts : (motion_lifecycle_opts_t){0};
    self->inited = true;
    return SW_OK;
}

sw_err_t motor_axis_run(motor_axis_t                *self,
                        hal_motor_dir_t              dir,
                        hal_motor_speed_t            speed,
                        const hal_motor_move_spec_t *spec)
{
    hal_motor_cmd_result_t r;
    sw_err_t               ret;

    if ((self == NULL) || !self->inited) {
        return SW_ERR_NOT_INIT;
    }
    if ((speed.kind != HAL_MOTOR_SPEED_FREQ) && (speed.kind != HAL_MOTOR_SPEED_GEAR)) {
        return SW_ERR_PARAM;
    }
    if (speed.value < 0) {
        return SW_ERR_PARAM;
    }
    if (speed.value == 0) {
        return motor_axis_stop(self);
    }
    if (motor_axis_state(self) == MOTOR_AXIS_STATE_FAULT) {
        return SW_ERR_STATE;
    }

    if ((spec == NULL) && (motor_axis_state(self) == MOTOR_AXIS_STATE_MOVING)) {
        r = hal_motor_set_speed(self->exec, self->motor, speed, dir);
    } else if (spec == NULL) {
        r = hal_motor_run_continuous(self->exec, self->motor, speed, dir);
    } else {
        r = hal_motor_move_to(self->exec, self->motor, speed, dir, spec);
    }

    ret = hal_motor_cmd_ok(r) ? SW_OK : SW_ERR_STATE;
    if (ret != SW_OK) {
        motion_lifecycle_report_fault(&self->opts, dir == HAL_MOTOR_DIR_FORWARD, motor_axis_fault_code(self));
    }
    return ret;
}

sw_err_t motor_axis_stop(motor_axis_t *self)
{
    hal_motor_cmd_result_t r;

    if ((self == NULL) || !self->inited) {
        return SW_ERR_NOT_INIT;
    }

    r = hal_motor_stop(self->exec, self->motor);
    if (hal_motor_cmd_ok(r)) {
        motion_lifecycle_publish_completed(&self->opts);
    }
    return hal_motor_cmd_ok(r) ? SW_OK : SW_ERR_STATE;
}

sw_err_t motor_axis_home(motor_axis_t *self)
{
    hal_motor_cmd_result_t r;

    if ((self == NULL) || !self->inited) {
        return SW_ERR_NOT_INIT;
    }
    if (motor_axis_state(self) == MOTOR_AXIS_STATE_FAULT) {
        return SW_ERR_STATE;
    }

    r = hal_motor_home(self->exec, self->motor);
    return hal_motor_cmd_ok(r) ? SW_OK : SW_ERR_STATE;
}

motor_axis_state_t motor_axis_state(const motor_axis_t *self)
{
    return axis_phase_to_state(self);
}

hal_motor_dir_t motor_axis_direction(const motor_axis_t *self)
{
    if ((self == NULL) || !self->inited) {
        return HAL_MOTOR_DIR_FORWARD;
    }
    return hal_motor_direction(self->exec, self->motor);
}

int64_t motor_axis_position(const motor_axis_t *self)
{
    if ((self == NULL) || !self->inited) {
        return 0;
    }
    return hal_motor_position(self->exec, self->motor);
}

hal_motor_fault_code_t motor_axis_fault_code(const motor_axis_t *self)
{
    if ((self == NULL) || !self->inited) {
        return HAL_MOTOR_FAULT_NONE;
    }
    return hal_motor_fault_code(self->exec, self->motor);
}

sw_err_t motor_axis_recover(motor_axis_t *self, hal_motor_recovery_step_t step)
{
    hal_motor_cmd_result_t r;

    if ((self == NULL) || !self->inited) {
        return SW_ERR_NOT_INIT;
    }

    r = hal_motor_recover(self->exec, self->motor, step);
    if (hal_motor_cmd_ok(r)) {
        motion_lifecycle_publish_completed(&self->opts);
    }
    return hal_motor_cmd_ok(r) ? SW_OK : SW_ERR_STATE;
}
