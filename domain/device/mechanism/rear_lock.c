/**
 * @file    rear_lock.c
 * @brief   后轮锁止机构领域层实现。
 *
 * 以 motor_executor_t 为底层，提供锁止/释放语义的推杆控制。
 * 限位停止、原点建立、故障恢复均由 MCC 执行器负责，
 * 本层仅做语义映射与状态聚合。
 */

#include "domain/device/mechanism/rear_lock.h"
#include <stddef.h>

/* -------------------- 静态模块状态 -------------------- */

static motor_executor_t *s_exec;
static int               s_motor;

/* -------------------- 公共 API -------------------- */

sw_err_t rear_lock_init(motor_executor_t *exec, int motor)
{
    if (exec == NULL) {
        return SW_ERR_PARAM;
    }
    s_exec  = exec;
    s_motor = motor;
    return SW_OK;
}

sw_err_t rear_lock_lock(int speed_gear, const motor_move_spec_t *spec)
{
    motor_cmd_result_t r;

    if (s_exec == NULL) {
        return SW_ERR_NOT_INIT;
    }
    if (rear_lock_state() == REAR_LOCK_STATE_FAULT) {
        return SW_ERR_STATE;
    }

    if (spec == NULL) {
        r = motor_run_continuous(s_exec, s_motor, motor_speed_gear(speed_gear), MOTOR_DIR_FORWARD);
    } else {
        r = motor_move_to(s_exec, s_motor, motor_speed_gear(speed_gear), MOTOR_DIR_FORWARD, spec);
    }
    return motor_cmd_ok(r) ? SW_OK : SW_ERR_STATE;
}

sw_err_t rear_lock_release(int speed_gear, const motor_move_spec_t *spec)
{
    motor_cmd_result_t r;

    if (s_exec == NULL) {
        return SW_ERR_NOT_INIT;
    }
    if (rear_lock_state() == REAR_LOCK_STATE_FAULT) {
        return SW_ERR_STATE;
    }

    if (spec == NULL) {
        r = motor_run_continuous(s_exec, s_motor, motor_speed_gear(speed_gear), MOTOR_DIR_REVERSE);
    } else {
        r = motor_move_to(s_exec, s_motor, motor_speed_gear(speed_gear), MOTOR_DIR_REVERSE, spec);
    }
    return motor_cmd_ok(r) ? SW_OK : SW_ERR_STATE;
}

sw_err_t rear_lock_stop(void)
{
    if (s_exec == NULL) {
        return SW_ERR_NOT_INIT;
    }
    (void)motor_stop(s_exec, s_motor);
    return SW_OK;
}

sw_err_t rear_lock_home(void)
{
    if (s_exec == NULL) {
        return SW_ERR_NOT_INIT;
    }
    if (rear_lock_state() == REAR_LOCK_STATE_FAULT) {
        return SW_ERR_STATE;
    }
    (void)motor_home(s_exec, s_motor);
    return SW_OK;
}

rear_lock_state_t rear_lock_state(void)
{
    motor_phase_t ph;

    if (s_exec == NULL) {
        return REAR_LOCK_STATE_IDLE;
    }

    ph = motor_phase(s_exec, s_motor);
    switch (ph) {
    case MOTOR_PHASE_STOPPED:
    case MOTOR_PHASE_WAITING_START:
    case MOTOR_PHASE_PAUSED:
        return REAR_LOCK_STATE_IDLE;

    case MOTOR_PHASE_RUNNING:
        return (motor_direction(s_exec, s_motor) == MOTOR_DIR_FORWARD)
               ? REAR_LOCK_STATE_LOCKING
               : REAR_LOCK_STATE_RELEASING;

    case MOTOR_PHASE_DECELERATING:
    case MOTOR_PHASE_REVERSAL_WAIT:
        return REAR_LOCK_STATE_STOPPING;

    case MOTOR_PHASE_FAULT:
    case MOTOR_PHASE_ESTOP:
        return REAR_LOCK_STATE_FAULT;

    default:
        return REAR_LOCK_STATE_IDLE;
    }
}

motor_fault_code_t rear_lock_fault_code(void)
{
    if (s_exec == NULL) {
        return MOTOR_FAULT_NONE;
    }
    return motor_fault_code(s_exec, s_motor);
}

sw_err_t rear_lock_recover(motor_recovery_step_t step)
{
    if (s_exec == NULL) {
        return SW_ERR_NOT_INIT;
    }
    (void)motor_recover(s_exec, s_motor, step);
    return SW_OK;
}
