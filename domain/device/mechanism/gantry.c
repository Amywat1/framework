/**
 * @file    gantry.c
 * @brief   龙门行走机构领域层实现。
 *
 * 以 motor_executor_t 为底层，提供方向感知的行走控制。
 * 编码器基准建立、限位停止、故障恢复均由 MCC 执行器负责，
 * 本层仅做方向映射与状态聚合。
 */

#include "domain/device/mechanism/gantry.h"
#include <stddef.h>

/* -------------------- 静态模块状态 -------------------- */

static motor_executor_t *s_exec;

/* -------------------- 公共 API -------------------- */

sw_err_t gantry_init(motor_executor_t *exec)
{
    if (exec == NULL) {
        return SW_ERR_PARAM;
    }
    s_exec = exec;
    return SW_OK;
}

sw_err_t gantry_move_fwd(int speed_gear, const motor_move_spec_t *spec)
{
    motor_cmd_result_t r;

    if (s_exec == NULL) {
        return SW_ERR_NOT_INIT;
    }
    if (gantry_state() == GANTRY_STATE_FAULT) {
        return SW_ERR_STATE;
    }

    if (spec == NULL) {
        r = motor_run_continuous(s_exec, 0, motor_speed_gear(speed_gear), MOTOR_DIR_FORWARD);
    } else {
        r = motor_move_to(s_exec, 0, motor_speed_gear(speed_gear), MOTOR_DIR_FORWARD, spec);
    }
    return motor_cmd_ok(r) ? SW_OK : SW_ERR_STATE;
}

sw_err_t gantry_move_rev(int speed_gear, const motor_move_spec_t *spec)
{
    motor_cmd_result_t r;

    if (s_exec == NULL) {
        return SW_ERR_NOT_INIT;
    }
    if (gantry_state() == GANTRY_STATE_FAULT) {
        return SW_ERR_STATE;
    }

    if (spec == NULL) {
        r = motor_run_continuous(s_exec, 0, motor_speed_gear(speed_gear), MOTOR_DIR_REVERSE);
    } else {
        r = motor_move_to(s_exec, 0, motor_speed_gear(speed_gear), MOTOR_DIR_REVERSE, spec);
    }
    return motor_cmd_ok(r) ? SW_OK : SW_ERR_STATE;
}

sw_err_t gantry_stop(void)
{
    if (s_exec == NULL) {
        return SW_ERR_NOT_INIT;
    }
    (void)motor_stop(s_exec, 0);
    return SW_OK;
}

sw_err_t gantry_home(void)
{
    if (s_exec == NULL) {
        return SW_ERR_NOT_INIT;
    }
    if (gantry_state() == GANTRY_STATE_FAULT) {
        return SW_ERR_STATE;
    }
    (void)motor_home(s_exec, 0);
    return SW_OK;
}

void gantry_tick(void)
{
    if (s_exec == NULL) {
        return;
    }
    motor_tick(s_exec);
}

gantry_state_t gantry_state(void)
{
    motor_phase_t ph;

    if (s_exec == NULL) {
        return GANTRY_STATE_IDLE;
    }

    ph = motor_phase(s_exec, 0);
    switch (ph) {
    case MOTOR_PHASE_STOPPED:
    case MOTOR_PHASE_WAITING_START:
    case MOTOR_PHASE_PAUSED:
        return GANTRY_STATE_IDLE;

    case MOTOR_PHASE_RUNNING:
        return (motor_direction(s_exec, 0) == MOTOR_DIR_FORWARD)
               ? GANTRY_STATE_MOVING_FWD
               : GANTRY_STATE_MOVING_REV;

    case MOTOR_PHASE_DECELERATING:
    case MOTOR_PHASE_REVERSAL_WAIT:
        return GANTRY_STATE_STOPPING;

    case MOTOR_PHASE_FAULT:
    case MOTOR_PHASE_ESTOP:
        return GANTRY_STATE_FAULT;

    default:
        return GANTRY_STATE_IDLE;
    }
}

int64_t gantry_position(void)
{
    if (s_exec == NULL) {
        return 0;
    }
    return motor_position(s_exec, 0);
}

motor_fault_code_t gantry_fault_code(void)
{
    if (s_exec == NULL) {
        return MOTOR_FAULT_NONE;
    }
    return motor_fault_code(s_exec, 0);
}

sw_err_t gantry_recover(motor_recovery_step_t step)
{
    if (s_exec == NULL) {
        return SW_ERR_NOT_INIT;
    }
    (void)motor_recover(s_exec, 0, step);
    return SW_OK;
}
