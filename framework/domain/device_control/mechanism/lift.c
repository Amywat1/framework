/**
 * @file    lift.c
 * @brief   顶刷升降机构领域层实现。
 *
 * 以 hal_motor_exec_t 为底层，提供方向感知的升降控制。
 * 限位停止、编码器基准建立、故障恢复均由 MCC 执行器负责，
 * 本层仅做方向映射与状态聚合。
 */

#include "framework/domain/device_control/mechanism/lift.h"
#include <stddef.h>

/* -------------------- 静态模块状态 -------------------- */

static hal_motor_exec_t *s_exec;
static int               s_motor;

/* -------------------- 公共 API -------------------- */

sw_err_t lift_init(hal_motor_exec_t *exec, int motor)
{
    if (exec == NULL) {
        return SW_ERR_PARAM;
    }
    s_exec  = exec;
    s_motor = motor;
    return SW_OK;
}

sw_err_t lift_up(int speed_gear, const hal_motor_move_spec_t *spec)
{
    hal_motor_cmd_result_t r;

    if (s_exec == NULL) {
        return SW_ERR_NOT_INIT;
    }
    if (lift_state() == LIFT_STATE_FAULT) {
        return SW_ERR_STATE;
    }

    if (spec == NULL) {
        r = hal_motor_run_continuous(s_exec, s_motor, hal_motor_speed_gear(speed_gear), HAL_MOTOR_DIR_FORWARD);
    } else {
        r = hal_motor_move_to(s_exec, s_motor, hal_motor_speed_gear(speed_gear), HAL_MOTOR_DIR_FORWARD, spec);
    }
    return hal_motor_cmd_ok(r) ? SW_OK : SW_ERR_STATE;
}

sw_err_t lift_down(int speed_gear, const hal_motor_move_spec_t *spec)
{
    hal_motor_cmd_result_t r;

    if (s_exec == NULL) {
        return SW_ERR_NOT_INIT;
    }
    if (lift_state() == LIFT_STATE_FAULT) {
        return SW_ERR_STATE;
    }

    if (spec == NULL) {
        r = hal_motor_run_continuous(s_exec, s_motor, hal_motor_speed_gear(speed_gear), HAL_MOTOR_DIR_REVERSE);
    } else {
        r = hal_motor_move_to(s_exec, s_motor, hal_motor_speed_gear(speed_gear), HAL_MOTOR_DIR_REVERSE, spec);
    }
    return hal_motor_cmd_ok(r) ? SW_OK : SW_ERR_STATE;
}

sw_err_t lift_stop(void)
{
    if (s_exec == NULL) {
        return SW_ERR_NOT_INIT;
    }
    (void)hal_motor_stop(s_exec, s_motor);
    return SW_OK;
}

sw_err_t lift_home(void)
{
    if (s_exec == NULL) {
        return SW_ERR_NOT_INIT;
    }
    if (lift_state() == LIFT_STATE_FAULT) {
        return SW_ERR_STATE;
    }
    (void)hal_motor_home(s_exec, s_motor);
    return SW_OK;
}

lift_state_t lift_state(void)
{
    hal_motor_phase_t ph;

    if (s_exec == NULL) {
        return LIFT_STATE_IDLE;
    }

    ph = hal_motor_phase(s_exec, s_motor);
    switch (ph) {
    case HAL_MOTOR_PHASE_STOPPED:
    case HAL_MOTOR_PHASE_WAITING_START:
    case HAL_MOTOR_PHASE_PAUSED:
        return LIFT_STATE_IDLE;

    case HAL_MOTOR_PHASE_RUNNING:
        return (hal_motor_direction(s_exec, s_motor) == HAL_MOTOR_DIR_FORWARD)
               ? LIFT_STATE_LIFTING
               : LIFT_STATE_LOWERING;

    case HAL_MOTOR_PHASE_DECELERATING:
    case HAL_MOTOR_PHASE_REVERSAL_WAIT:
        return LIFT_STATE_STOPPING;

    case HAL_MOTOR_PHASE_FAULT:
    case HAL_MOTOR_PHASE_ESTOP:
        return LIFT_STATE_FAULT;

    default:
        return LIFT_STATE_IDLE;
    }
}

int64_t lift_position(void)
{
    if (s_exec == NULL) {
        return 0;
    }
    return hal_motor_position(s_exec, s_motor);
}

hal_motor_fault_code_t lift_fault_code(void)
{
    if (s_exec == NULL) {
        return HAL_MOTOR_FAULT_NONE;
    }
    return hal_motor_fault_code(s_exec, s_motor);
}

sw_err_t lift_recover(hal_motor_recovery_step_t step)
{
    if (s_exec == NULL) {
        return SW_ERR_NOT_INIT;
    }
    (void)hal_motor_recover(s_exec, s_motor, step);
    return SW_OK;
}
