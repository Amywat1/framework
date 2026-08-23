/**
 * @file    motor_axis.c
 * @brief   单轴运动模式实现
 */

#include "domain/mechanism/patterns/motor_axis.h"

#include <stddef.h>
#include <string.h>

static motor_axis_state_t exec_state_to_axis_state(const motor_axis_t *self)
{
    motor_exec_state_t ph;

    if ((self == NULL) || !self->inited) {
        return MOTOR_AXIS_STATE_IDLE;
    }

    ph = motor_exec_state(self->exec, self->motor);
    switch (ph) {
    case MOTOR_STATE_STOPPED:
        return MOTOR_AXIS_STATE_IDLE;

    case MOTOR_STATE_WAITING_START:
    case MOTOR_STATE_RUNNING:
    case MOTOR_STATE_STOPPING:
    case MOTOR_STATE_REVERSAL_WAIT:
        return MOTOR_AXIS_STATE_MOVING;

    case MOTOR_STATE_FAULT:
    case MOTOR_STATE_ESTOP:
        return MOTOR_AXIS_STATE_FAULT;

    default:
        return MOTOR_AXIS_STATE_IDLE;
    }
}

static bool is_end_event(motor_event_type_t type)
{
    switch (type) {
    case MOTOR_EVENT_ARRIVED:
    case MOTOR_EVENT_TIMEOUT:
    case MOTOR_EVENT_STOPPED:
    case MOTOR_EVENT_FAULT:
    case MOTOR_EVENT_ESTOP:
        return true;
    case MOTOR_EVENT_WARNING:
    default:
        return false;
    }
}

static void report_end(motor_axis_t *self, const motor_event_t *ev)
{
    /* 同一故障闩锁下，事件路径与拒令合成可能先后到达；FAULT/ESTOP 按码去重。 */
    if (self->last_valid && ((ev->type == MOTOR_EVENT_FAULT) || (ev->type == MOTOR_EVENT_ESTOP))
        && (self->last_event.type == ev->type) && (self->last_event.fault == ev->fault)) {
        return;
    }

    self->last_valid = true;
    self->last_event = *ev;
    if (self->opts.on_motion_end != NULL) {
        self->opts.on_motion_end(self->opts.motion_actuator_id, ev);
    }
}

/**
 * @brief  命令被拒且轴已处于故障/急停（或已有故障码）时合成结局
 * @note   互锁等纯拒绝只返回 SW_ERR_STATE，不冒充运行故障结局。
 */
static void report_cmd_fault(motor_axis_t *self)
{
    motor_event_t           ev;
    motor_exec_fault_code_t fault;
    motor_exec_state_t      exec_state;

    fault = motor_exec_fault_code(self->exec, self->motor);
    exec_state = motor_exec_state(self->exec, self->motor);
    if ((fault == MOTOR_FAULT_NONE) && (exec_state != MOTOR_STATE_FAULT) && (exec_state != MOTOR_STATE_ESTOP)) {
        return;
    }

    memset(&ev, 0, sizeof(ev));
    ev.motor = self->motor;
    ev.type  = (exec_state == MOTOR_STATE_ESTOP) ? MOTOR_EVENT_ESTOP : MOTOR_EVENT_FAULT;
    ev.fault = fault;
    report_end(self, &ev);
}

static void drain_motor_events(motor_axis_t *self)
{
    motor_event_t ev;

    while (motor_exec_pop_event_for(self->exec, self->motor, &ev)) {
        if (!is_end_event(ev.type)) {
            continue;
        }
        report_end(self, &ev);
    }
}

sw_err_t motor_axis_init(motor_axis_t *self, motor_exec_t *exec, int motor, const motion_lifecycle_opts_t *opts)
{
    if ((self == NULL) || (exec == NULL)) {
        return SW_ERR_PARAM;
    }

    self->exec          = exec;
    self->motor         = motor;
    self->opts          = (opts != NULL) ? *opts : (motion_lifecycle_opts_t){0};
    self->inited        = true;
    self->awaiting_idle = false;
    self->last_valid    = false;
    memset(&self->last_event, 0, sizeof(self->last_event));
    return SW_OK;
}

sw_err_t motor_axis_run(motor_axis_t *self, motor_dir_t dir, motor_speed_t speed, const motor_move_spec_t *spec)
{
    motor_cmd_result_t r;
    sw_err_t           ret;

    if ((self == NULL) || !self->inited) {
        return SW_ERR_NOT_INIT;
    }
    if ((speed.kind != MOTOR_SPEED_FREQ) && (speed.kind != MOTOR_SPEED_GEAR)) {
        return SW_ERR_PARAM;
    }
    if (speed.value <= 0) {
        return SW_ERR_PARAM;
    }

    r = motor_exec_run(self->exec, self->motor, speed, dir, spec);

    ret = motor_cmd_ok(r) ? SW_OK : SW_ERR_STATE;
    if (ret == SW_OK) {
        self->awaiting_idle = true;
    } else {
        report_cmd_fault(self);
    }
    return ret;
}

sw_err_t motor_axis_home(motor_axis_t *self)
{
    motor_cmd_result_t r;

    if ((self == NULL) || !self->inited) {
        return SW_ERR_NOT_INIT;
    }

    r = motor_exec_home(self->exec, self->motor);
    if (motor_cmd_ok(r)) {
        self->awaiting_idle = true;
        return SW_OK;
    }
    report_cmd_fault(self);
    return SW_ERR_STATE;
}

sw_err_t motor_axis_stop(motor_axis_t *self)
{
    motor_cmd_result_t r;

    if ((self == NULL) || !self->inited) {
        return SW_ERR_NOT_INIT;
    }

    r = motor_exec_stop(self->exec, self->motor);
    if (motor_cmd_ok(r)) {
        self->awaiting_idle = true;
        return SW_OK;
    }
    report_cmd_fault(self);
    return SW_ERR_STATE;
}

motor_axis_state_t motor_axis_state(const motor_axis_t *self)
{
    return exec_state_to_axis_state(self);
}

bool motor_axis_last_result(const motor_axis_t *self, motor_event_t *out)
{
    if ((self == NULL) || !self->inited || !self->last_valid || (out == NULL)) {
        return false;
    }
    *out = self->last_event;
    return true;
}

sw_err_t motor_axis_recover(motor_axis_t *self)
{
    motor_cmd_result_t r;

    if ((self == NULL) || !self->inited) {
        return SW_ERR_NOT_INIT;
    }

    r = motor_exec_recover(self->exec, self->motor, MOTOR_RECOVERY_DRIVER_RESET);
    if (!motor_cmd_ok(r)) {
        report_cmd_fault(self);
        return SW_ERR_STATE;
    }

    r = motor_exec_recover(self->exec, self->motor, MOTOR_RECOVERY_MODULE_STOP);
    if (!motor_cmd_ok(r)) {
        report_cmd_fault(self);
        return SW_ERR_STATE;
    }

    /* 闩锁解除后允许再次上报同码故障结局。 */
    self->last_valid = false;
    memset(&self->last_event, 0, sizeof(self->last_event));
    self->awaiting_idle = true;
    return SW_OK;
}

void motor_axis_poll(motor_axis_t *self)
{
    if ((self == NULL) || !self->inited) {
        return;
    }

    drain_motor_events(self);

    if (!self->awaiting_idle) {
        return;
    }
    if (motor_axis_state(self) != MOTOR_AXIS_STATE_IDLE) {
        return;
    }

    self->awaiting_idle = false;
    if (self->opts.motion_actuator_id != 0U) {
        actuator_publish_motion_completed(self->opts.motion_actuator_id);
    }
}

bool motor_axis_is_settled(const motor_axis_t *self)
{
    if ((self == NULL) || !self->inited) {
        return false;
    }
    return !self->awaiting_idle && (motor_axis_state(self) != MOTOR_AXIS_STATE_MOVING);
}
