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
        return MOTOR_AXIS_STATE_IDLE;

    case HAL_MOTOR_PHASE_WAITING_START:
    case HAL_MOTOR_PHASE_PAUSED:
    case HAL_MOTOR_PHASE_RUNNING:
    case HAL_MOTOR_PHASE_DECELERATING:
    case HAL_MOTOR_PHASE_REVERSAL_WAIT:
        return MOTOR_AXIS_STATE_MOVING;

    case HAL_MOTOR_PHASE_FAULT:
    case HAL_MOTOR_PHASE_ESTOP:
        return MOTOR_AXIS_STATE_FAULT;

    default:
        return MOTOR_AXIS_STATE_IDLE;
    }
}

static void report_cmd_fault(motor_axis_t *self, bool is_positive_dir)
{
    motion_lifecycle_report_fault(&self->opts, is_positive_dir, hal_motor_fault_code(self->exec, self->motor));
}

sw_err_t motor_axis_init(motor_axis_t *self, hal_motor_exec_t *exec, int motor, const motion_lifecycle_opts_t *opts)
{
    if ((self == NULL) || (exec == NULL)) {
        return SW_ERR_PARAM;
    }

    self->exec           = exec;
    self->motor          = motor;
    self->opts           = (opts != NULL) ? *opts : (motion_lifecycle_opts_t){0};
    self->inited         = true;
    self->awaiting_idle  = false;
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
    if (speed.value <= 0) {
        return SW_ERR_PARAM;
    }

    if ((spec == NULL) && (hal_motor_phase(self->exec, self->motor) == HAL_MOTOR_PHASE_RUNNING)) {
        r = hal_motor_set_speed(self->exec, self->motor, speed, dir);
    } else if (spec == NULL) {
        r = hal_motor_run_continuous(self->exec, self->motor, speed, dir);
    } else {
        r = hal_motor_move_to(self->exec, self->motor, speed, dir, spec);
    }

    ret = hal_motor_cmd_ok(r) ? SW_OK : SW_ERR_STATE;
    if (ret == SW_OK) {
        self->awaiting_idle = true;
    } else {
        report_cmd_fault(self, dir == HAL_MOTOR_DIR_FORWARD);
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
        self->awaiting_idle = true;
        return SW_OK;
    }
    report_cmd_fault(self, hal_motor_direction(self->exec, self->motor) == HAL_MOTOR_DIR_FORWARD);
    return SW_ERR_STATE;
}

motor_axis_state_t motor_axis_state(const motor_axis_t *self)
{
    return axis_phase_to_state(self);
}

sw_err_t motor_axis_recover(motor_axis_t *self)
{
    hal_motor_cmd_result_t r;

    if ((self == NULL) || !self->inited) {
        return SW_ERR_NOT_INIT;
    }

    r = hal_motor_recover(self->exec, self->motor, HAL_MOTOR_RECOVERY_DRIVER_RESET);
    if (!hal_motor_cmd_ok(r)) {
        report_cmd_fault(self, hal_motor_direction(self->exec, self->motor) == HAL_MOTOR_DIR_FORWARD);
        return SW_ERR_STATE;
    }

    r = hal_motor_recover(self->exec, self->motor, HAL_MOTOR_RECOVERY_MODULE_STOP);
    if (!hal_motor_cmd_ok(r)) {
        report_cmd_fault(self, hal_motor_direction(self->exec, self->motor) == HAL_MOTOR_DIR_FORWARD);
        return SW_ERR_STATE;
    }

    self->awaiting_idle = true;
    return SW_OK;
}

void motor_axis_poll(motor_axis_t *self)
{
    if ((self == NULL) || !self->inited) {
        return;
    }
    if (!self->awaiting_idle) {
        return;
    }
    if (motor_axis_state(self) != MOTOR_AXIS_STATE_IDLE) {
        return;
    }

    self->awaiting_idle = false;
    motion_lifecycle_publish_completed(&self->opts);
}
