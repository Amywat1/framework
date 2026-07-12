/**
 * @file    bidirectional_motion.c
 * @brief   双向往复运动模式实现
 * @author  HUWANGWEI
 * @date    2026-07-11
 */

#include "domain/device_control/patterns/bidirectional_motion.h"

#include <stddef.h>

static void bidir_publish_motion_completed(const bidirectional_motion_t *self)
{
    if ((self != NULL) && (self->opts.motion_actuator_id != 0U)) {
        actuator_publish_motion_completed(self->opts.motion_actuator_id);
    }
}

static void bidir_report_process_fault(const bidirectional_motion_t *self,
                                       hal_motor_dir_t               dir,
                                       hal_motor_fault_code_t        fault)
{
    if ((self != NULL) && (self->opts.on_process_fault != NULL)) {
        self->opts.on_process_fault(dir == HAL_MOTOR_DIR_FORWARD, fault);
    }
}

static bidir_motion_state_t bidir_phase_to_state(const bidirectional_motion_t *self)
{
    hal_motor_phase_t ph;

    if ((self == NULL) || !self->inited) {
        return BIDIR_MOTION_STATE_IDLE;
    }

    ph = hal_motor_phase(self->exec, self->motor);
    switch (ph) {
    case HAL_MOTOR_PHASE_STOPPED:
    case HAL_MOTOR_PHASE_WAITING_START:
    case HAL_MOTOR_PHASE_PAUSED:
        return BIDIR_MOTION_STATE_IDLE;

    case HAL_MOTOR_PHASE_RUNNING:
        return BIDIR_MOTION_STATE_MOVING;

    case HAL_MOTOR_PHASE_DECELERATING:
    case HAL_MOTOR_PHASE_REVERSAL_WAIT:
        return BIDIR_MOTION_STATE_STOPPING;

    case HAL_MOTOR_PHASE_FAULT:
    case HAL_MOTOR_PHASE_ESTOP:
        return BIDIR_MOTION_STATE_FAULT;

    default:
        return BIDIR_MOTION_STATE_IDLE;
    }
}

sw_err_t bidirectional_motion_init(bidirectional_motion_t        *self,
                                   hal_motor_exec_t              *exec,
                                   int                            motor,
                                   const motion_lifecycle_opts_t *opts)
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

sw_err_t bidirectional_motion_run(bidirectional_motion_t      *self,
                                  hal_motor_dir_t              dir,
                                  int                          speed_gear,
                                  const hal_motor_move_spec_t *spec)
{
    hal_motor_cmd_result_t r;
    sw_err_t               ret;

    if ((self == NULL) || !self->inited) {
        return SW_ERR_NOT_INIT;
    }
    if (bidirectional_motion_state(self) == BIDIR_MOTION_STATE_FAULT) {
        return SW_ERR_STATE;
    }

    if (spec == NULL) {
        r = hal_motor_run_continuous(self->exec, self->motor, hal_motor_speed_gear(speed_gear), dir);
    } else {
        r = hal_motor_move_to(self->exec, self->motor, hal_motor_speed_gear(speed_gear), dir, spec);
    }

    ret = hal_motor_cmd_ok(r) ? SW_OK : SW_ERR_STATE;
    if (ret != SW_OK) {
        bidir_report_process_fault(self, dir, bidirectional_motion_fault_code(self));
    }
    return ret;
}

sw_err_t bidirectional_motion_stop(bidirectional_motion_t *self)
{
    hal_motor_cmd_result_t r;

    if ((self == NULL) || !self->inited) {
        return SW_ERR_NOT_INIT;
    }

    r = hal_motor_stop(self->exec, self->motor);
    if (hal_motor_cmd_ok(r)) {
        bidir_publish_motion_completed(self);
    }
    return hal_motor_cmd_ok(r) ? SW_OK : SW_ERR_STATE;
}

sw_err_t bidirectional_motion_home(bidirectional_motion_t *self)
{
    hal_motor_cmd_result_t r;

    if ((self == NULL) || !self->inited) {
        return SW_ERR_NOT_INIT;
    }
    if (bidirectional_motion_state(self) == BIDIR_MOTION_STATE_FAULT) {
        return SW_ERR_STATE;
    }

    r = hal_motor_home(self->exec, self->motor);
    return hal_motor_cmd_ok(r) ? SW_OK : SW_ERR_STATE;
}

bidir_motion_state_t bidirectional_motion_state(const bidirectional_motion_t *self)
{
    return bidir_phase_to_state(self);
}

hal_motor_dir_t bidirectional_motion_direction(const bidirectional_motion_t *self)
{
    if ((self == NULL) || !self->inited) {
        return HAL_MOTOR_DIR_FORWARD;
    }
    return hal_motor_direction(self->exec, self->motor);
}

int64_t bidirectional_motion_position(const bidirectional_motion_t *self)
{
    if ((self == NULL) || !self->inited) {
        return 0;
    }
    return hal_motor_position(self->exec, self->motor);
}

hal_motor_fault_code_t bidirectional_motion_fault_code(const bidirectional_motion_t *self)
{
    if ((self == NULL) || !self->inited) {
        return HAL_MOTOR_FAULT_NONE;
    }
    return hal_motor_fault_code(self->exec, self->motor);
}

sw_err_t bidirectional_motion_recover(bidirectional_motion_t *self, hal_motor_recovery_step_t step)
{
    hal_motor_cmd_result_t r;

    if ((self == NULL) || !self->inited) {
        return SW_ERR_NOT_INIT;
    }

    r = hal_motor_recover(self->exec, self->motor, step);
    if (hal_motor_cmd_ok(r)) {
        bidir_publish_motion_completed(self);
    }
    return hal_motor_cmd_ok(r) ? SW_OK : SW_ERR_STATE;
}
