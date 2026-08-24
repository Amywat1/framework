/**
 * @file    motor_axis.c
 * @brief   单轴运动会话实现
 */

#include "domain/mechanism/patterns/motor_axis.h"

#include "domain/ports/outbound/motor/motor_exec_port.h"

#include <stddef.h>
#include <string.h>

static motor_cmd_result_t axis_reject(motor_cmd_reject_t reject, const char *reason)
{
    motor_cmd_result_t r;

    r.status = MOTOR_CMD_REJECTED;
    r.reject = reject;
    r.reason = reason;
    return r;
}

static bool axis_ready(const motor_axis_t *self)
{
    return (self != NULL) && self->inited && (self->exec != NULL);
}

static motor_axis_state_t exec_state_to_axis_state(const motor_axis_t *self)
{
    motor_exec_state_t ph;

    if (!axis_ready(self)) {
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
 * @note   互锁等纯拒绝只返回拒绝码，不冒充运行故障结局。
 */
static void report_cmd_fault(motor_axis_t *self)
{
    motor_event_t           ev;
    motor_exec_fault_code_t fault;
    motor_exec_state_t      exec_state;

    fault      = motor_exec_fault_code(self->exec, self->motor);
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

/**
 * @brief  运动命令已受理：解除 FAULT/ESTOP 去重闩并标记待空闲
 * @note   与显式 recover 成功相同，否则可续动后再同码故障会被当成闩内重复丢掉。
 */
static void on_motion_accepted(motor_axis_t *self)
{
    self->last_valid    = false;
    self->awaiting_idle = true;
    memset(&self->last_event, 0, sizeof(self->last_event));
}

static motor_cmd_result_t finish_motion_cmd(motor_axis_t *self, motor_cmd_result_t r)
{
    if (motor_cmd_ok(r)) {
        on_motion_accepted(self);
    } else {
        report_cmd_fault(self);
    }
    return r;
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

motor_cmd_result_t motor_axis_run(motor_axis_t            *self,
                                  motor_dir_t              dir,
                                  motor_speed_t            speed,
                                  const motor_move_spec_t *spec)
{
    motor_cmd_result_t r;

    if (!axis_ready(self)) {
        return axis_reject(MOTOR_REJECT_UNAVAILABLE, "axis-not-init");
    }
    if ((speed.kind != MOTOR_SPEED_FREQ) && (speed.kind != MOTOR_SPEED_GEAR)) {
        return axis_reject(MOTOR_REJECT_BAD_SPEED, "bad-speed");
    }
    if (speed.value <= 0) {
        return axis_reject(MOTOR_REJECT_BAD_SPEED, "bad-speed");
    }

    r = motor_exec_run(self->exec, self->motor, speed, dir, spec);
    return finish_motion_cmd(self, r);
}

motor_cmd_result_t motor_axis_home(motor_axis_t *self)
{
    motor_cmd_result_t r;

    if (!axis_ready(self)) {
        return axis_reject(MOTOR_REJECT_UNAVAILABLE, "axis-not-init");
    }

    r = motor_exec_home(self->exec, self->motor);
    return finish_motion_cmd(self, r);
}

motor_cmd_result_t motor_axis_stop(motor_axis_t *self)
{
    motor_cmd_result_t r;

    if (!axis_ready(self)) {
        return axis_reject(MOTOR_REJECT_UNAVAILABLE, "axis-not-init");
    }

    r = motor_exec_stop(self->exec, self->motor);
    if (motor_cmd_ok(r)) {
        self->awaiting_idle = true;
        return r;
    }
    report_cmd_fault(self);
    return r;
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

motor_cmd_result_t motor_axis_recover(motor_axis_t *self)
{
    motor_cmd_result_t r;

    if (!axis_ready(self)) {
        return axis_reject(MOTOR_REJECT_UNAVAILABLE, "axis-not-init");
    }

    r = motor_exec_recover(self->exec, self->motor, MOTOR_RECOVERY_DRIVER_RESET);
    if (!motor_cmd_ok(r)) {
        report_cmd_fault(self);
        return r;
    }

    r = motor_exec_recover(self->exec, self->motor, MOTOR_RECOVERY_MODULE_STOP);
    if (!motor_cmd_ok(r)) {
        report_cmd_fault(self);
        return r;
    }

    on_motion_accepted(self);
    return r;
}

void motor_axis_poll(motor_axis_t *self)
{
    if (!axis_ready(self)) {
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
    if (!axis_ready(self)) {
        return false;
    }
    return !self->awaiting_idle && (motor_axis_state(self) != MOTOR_AXIS_STATE_MOVING);
}

int64_t motor_axis_position(const motor_axis_t *self)
{
    if (!axis_ready(self)) {
        return 0;
    }
    return motor_exec_position(self->exec, self->motor);
}

motor_dir_t motor_axis_direction(const motor_axis_t *self)
{
    if (!axis_ready(self)) {
        return MOTOR_DIR_FORWARD;
    }
    return motor_exec_direction(self->exec, self->motor);
}

motor_exec_fault_code_t motor_axis_fault_code(const motor_axis_t *self)
{
    if (!axis_ready(self)) {
        return MOTOR_FAULT_NONE;
    }
    return motor_exec_fault_code(self->exec, self->motor);
}

bool motor_axis_fault_requires_confirm(const motor_axis_t *self, motor_exec_fault_code_t code)
{
    if (!axis_ready(self)) {
        return true;
    }
    return motor_exec_fault_requires_confirm(self->exec, self->motor, code);
}

bool motor_axis_baseline_trusted(const motor_axis_t *self)
{
    if (!axis_ready(self)) {
        return false;
    }
    return motor_exec_baseline_trusted(self->exec, self->motor);
}

bool motor_axis_encoder_healthy(const motor_axis_t *self)
{
    if (!axis_ready(self)) {
        return false;
    }
    return motor_exec_encoder_healthy(self->exec, self->motor);
}

int motor_axis_current_freq(const motor_axis_t *self)
{
    if (!axis_ready(self)) {
        return 0;
    }
    return motor_exec_current_freq(self->exec, self->motor);
}

motor_cmd_result_t motor_axis_zero_encoder(motor_axis_t *self)
{
    if (!axis_ready(self)) {
        return axis_reject(MOTOR_REJECT_UNAVAILABLE, "axis-not-init");
    }
    return motor_exec_zero_encoder(self->exec, self->motor);
}

motor_cmd_result_t motor_axis_confirm_baseline(motor_axis_t *self)
{
    if (!axis_ready(self)) {
        return axis_reject(MOTOR_REJECT_UNAVAILABLE, "axis-not-init");
    }
    return motor_exec_confirm_baseline(self->exec, self->motor);
}
