/**
 * @file    motor_axis.c
 * @brief   单轴运动模式实现
 * @author  HUWANGWEI
 * @date    2026-07-17
 */

#include "domain/device_control/patterns/motor_axis.h"

#include <stddef.h>
#include <string.h>

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

static bool map_event_to_result(const hal_motor_event_t *ev, motor_axis_end_result_t *out)
{
    switch (ev->type) {
    case HAL_MOTOR_EVENT_ARRIVED:
    case HAL_MOTOR_EVENT_TIMEOUT:
    case HAL_MOTOR_EVENT_STOPPED:
    case HAL_MOTOR_EVENT_FAULT:
    case HAL_MOTOR_EVENT_ESTOP:
        break;
    case HAL_MOTOR_EVENT_WARNING:
    default:
        return false;
    }

    memset(out, 0, sizeof(*out));
    out->valid      = true;
    out->outcome    = ev->type;
    out->trigger    = ev->trigger;
    out->has_limit  = ev->has_limit;
    out->limit      = ev->limit;
    out->final_pos  = ev->final_pos;
    out->elapsed_ms = ev->elapsed_ms;
    out->fault      = ev->fault;
    return true;
}

static void report_end(motor_axis_t *self, const motor_axis_end_result_t *result)
{
    /* 同一故障闩锁下，事件路径与拒令合成可能先后到达；FAULT/ESTOP 按码去重。 */
    if (result->valid && self->last_result.valid
        && ((result->outcome == HAL_MOTOR_EVENT_FAULT)
            || (result->outcome == HAL_MOTOR_EVENT_ESTOP))
        && (self->last_result.outcome == result->outcome)
        && (self->last_result.fault == result->fault)) {
        return;
    }

    self->last_result = *result;
    motion_lifecycle_report_end(&self->opts, result);
}

/**
 * @brief  命令被拒且轴已处于故障/急停（或已有故障码）时合成结局
 * @note   互锁等纯拒绝只返回 SW_ERR_STATE，不冒充运行故障结局。
 * @note   与已记录的同码 FAULT/ESTOP 结局去重，避免拒令与事件双报。
 */
static void report_cmd_fault(motor_axis_t *self)
{
    motor_axis_end_result_t result;
    hal_motor_fault_code_t  fault;
    hal_motor_phase_t       phase;

    fault = hal_motor_fault_code(self->exec, self->motor);
    phase = hal_motor_phase(self->exec, self->motor);
    if ((fault == HAL_MOTOR_FAULT_NONE) && (phase != HAL_MOTOR_PHASE_FAULT)
        && (phase != HAL_MOTOR_PHASE_ESTOP)) {
        return;
    }

    memset(&result, 0, sizeof(result));
    result.valid   = true;
    result.outcome = (phase == HAL_MOTOR_PHASE_ESTOP) ? HAL_MOTOR_EVENT_ESTOP
                                                     : HAL_MOTOR_EVENT_FAULT;
    result.trigger = HAL_MOTOR_END_NONE;
    result.fault   = fault;
    report_end(self, &result);
}

static void drain_motor_events(motor_axis_t *self)
{
    hal_motor_event_t       ev;
    motor_axis_end_result_t result;

    while (hal_motor_pop_event_for(self->exec, self->motor, &ev)) {
        if (!map_event_to_result(&ev, &result)) {
            continue;
        }
        report_end(self, &result);
    }
}

sw_err_t motor_axis_init(motor_axis_t *self, hal_motor_exec_t *exec, int motor, const motion_lifecycle_opts_t *opts)
{
    if ((self == NULL) || (exec == NULL)) {
        return SW_ERR_PARAM;
    }

    self->exec          = exec;
    self->motor         = motor;
    self->opts          = (opts != NULL) ? *opts : (motion_lifecycle_opts_t){0};
    self->inited        = true;
    self->awaiting_idle = false;
    memset(&self->last_result, 0, sizeof(self->last_result));
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

    r = hal_motor_run(self->exec, self->motor, speed, dir, spec);

    ret = hal_motor_cmd_ok(r) ? SW_OK : SW_ERR_STATE;
    if (ret == SW_OK) {
        self->awaiting_idle = true;
    } else {
        report_cmd_fault(self);
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
    report_cmd_fault(self);
    return SW_ERR_STATE;
}

motor_axis_state_t motor_axis_state(const motor_axis_t *self)
{
    return axis_phase_to_state(self);
}

motor_axis_end_result_t motor_axis_last_result(const motor_axis_t *self)
{
    motor_axis_end_result_t empty;

    memset(&empty, 0, sizeof(empty));
    if ((self == NULL) || !self->inited) {
        return empty;
    }
    return self->last_result;
}

sw_err_t motor_axis_recover(motor_axis_t *self)
{
    hal_motor_cmd_result_t r;

    if ((self == NULL) || !self->inited) {
        return SW_ERR_NOT_INIT;
    }

    r = hal_motor_recover(self->exec, self->motor, HAL_MOTOR_RECOVERY_DRIVER_RESET);
    if (!hal_motor_cmd_ok(r)) {
        report_cmd_fault(self);
        return SW_ERR_STATE;
    }

    r = hal_motor_recover(self->exec, self->motor, HAL_MOTOR_RECOVERY_MODULE_STOP);
    if (!hal_motor_cmd_ok(r)) {
        report_cmd_fault(self);
        return SW_ERR_STATE;
    }

    /* 闩锁解除后允许再次上报同码故障结局。 */
    memset(&self->last_result, 0, sizeof(self->last_result));
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
    motion_lifecycle_publish_completed(&self->opts);
}

bool motor_axis_is_settled(const motor_axis_t *self)
{
    if ((self == NULL) || !self->inited) {
        return true;
    }
    return !self->awaiting_idle && (motor_axis_state(self) != MOTOR_AXIS_STATE_MOVING);
}
