/**
 * @file    rear_lock.c
 * @brief   后轮锁止机构领域层实现。
 *
 * 以 hal_motor_exec_t 为底层，提供锁止/释放语义的推杆控制。
 * 限位停止、原点建立、故障恢复均由 MCC 执行器负责，
 * 本层仅做语义映射与状态聚合。
 */

#include "framework/domain/device_control/mechanism/rear_lock.h"
#include <stddef.h>

/* -------------------- 静态模块状态 -------------------- */

static hal_motor_exec_t *s_exec;
static int               s_motor;

/* -------------------- 公共 API -------------------- */

sw_err_t rear_lock_init(hal_motor_exec_t *exec, int motor)
{
    if (exec == NULL) {
        return SW_ERR_PARAM;
    }
    s_exec  = exec;
    s_motor = motor;
    return SW_OK;
}

sw_err_t rear_lock_lock(int speed_gear, const hal_motor_move_spec_t *spec)
{
    hal_motor_cmd_result_t r;

    if (s_exec == NULL) {
        return SW_ERR_NOT_INIT;
    }
    if (rear_lock_state() == REAR_LOCK_STATE_FAULT) {
        return SW_ERR_STATE;
    }

    if (spec == NULL) {
        r = hal_motor_run_continuous(s_exec, s_motor, hal_motor_speed_gear(speed_gear), HAL_MOTOR_DIR_FORWARD);
    } else {
        r = hal_motor_move_to(s_exec, s_motor, hal_motor_speed_gear(speed_gear), HAL_MOTOR_DIR_FORWARD, spec);
    }
    return hal_motor_cmd_ok(r) ? SW_OK : SW_ERR_STATE;
}

sw_err_t rear_lock_release(int speed_gear, const hal_motor_move_spec_t *spec)
{
    hal_motor_cmd_result_t r;

    if (s_exec == NULL) {
        return SW_ERR_NOT_INIT;
    }
    if (rear_lock_state() == REAR_LOCK_STATE_FAULT) {
        return SW_ERR_STATE;
    }

    if (spec == NULL) {
        r = hal_motor_run_continuous(s_exec, s_motor, hal_motor_speed_gear(speed_gear), HAL_MOTOR_DIR_REVERSE);
    } else {
        r = hal_motor_move_to(s_exec, s_motor, hal_motor_speed_gear(speed_gear), HAL_MOTOR_DIR_REVERSE, spec);
    }
    return hal_motor_cmd_ok(r) ? SW_OK : SW_ERR_STATE;
}

sw_err_t rear_lock_stop(void)
{
    if (s_exec == NULL) {
        return SW_ERR_NOT_INIT;
    }
    (void)hal_motor_stop(s_exec, s_motor);
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
    (void)hal_motor_home(s_exec, s_motor);
    return SW_OK;
}

rear_lock_state_t rear_lock_state(void)
{
    hal_motor_phase_t ph;

    if (s_exec == NULL) {
        return REAR_LOCK_STATE_IDLE;
    }

    ph = hal_motor_phase(s_exec, s_motor);
    switch (ph) {
    case HAL_MOTOR_PHASE_STOPPED:
    case HAL_MOTOR_PHASE_WAITING_START:
    case HAL_MOTOR_PHASE_PAUSED:
        return REAR_LOCK_STATE_IDLE;

    case HAL_MOTOR_PHASE_RUNNING:
        return (hal_motor_direction(s_exec, s_motor) == HAL_MOTOR_DIR_FORWARD)
               ? REAR_LOCK_STATE_LOCKING
               : REAR_LOCK_STATE_RELEASING;

    case HAL_MOTOR_PHASE_DECELERATING:
    case HAL_MOTOR_PHASE_REVERSAL_WAIT:
        return REAR_LOCK_STATE_STOPPING;

    case HAL_MOTOR_PHASE_FAULT:
    case HAL_MOTOR_PHASE_ESTOP:
        return REAR_LOCK_STATE_FAULT;

    default:
        return REAR_LOCK_STATE_IDLE;
    }
}

hal_motor_fault_code_t rear_lock_fault_code(void)
{
    if (s_exec == NULL) {
        return HAL_MOTOR_FAULT_NONE;
    }
    return hal_motor_fault_code(s_exec, s_motor);
}

sw_err_t rear_lock_recover(hal_motor_recovery_step_t step)
{
    if (s_exec == NULL) {
        return SW_ERR_NOT_INIT;
    }
    (void)hal_motor_recover(s_exec, s_motor, step);
    return SW_OK;
}
