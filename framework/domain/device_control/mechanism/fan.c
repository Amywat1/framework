/**
 * @file    fan.c
 * @brief   风机机构领域层实现。
 *
 * 以 hal_motor_exec_t 为底层，风机仅正转、单挡运行。
 * 故障检测、恢复均由 MCC 执行器负责，本层仅做状态聚合。
 */

#include "framework/domain/device_control/mechanism/fan.h"
#include <stddef.h>

/* -------------------- 静态模块状态 -------------------- */

static hal_motor_exec_t *s_exec;
static int               s_motor;

/* -------------------- 公共 API -------------------- */

sw_err_t fan_init(hal_motor_exec_t *exec, int motor)
{
    if (exec == NULL) {
        return SW_ERR_PARAM;
    }
    s_exec  = exec;
    s_motor = motor;
    return SW_OK;
}

sw_err_t fan_start(void)
{
    hal_motor_cmd_result_t r;

    if (s_exec == NULL) {
        return SW_ERR_NOT_INIT;
    }
    if (fan_state() == FAN_STATE_FAULT) {
        return SW_ERR_STATE;
    }

    r = hal_motor_run_continuous(s_exec, s_motor, hal_motor_speed_gear(0), HAL_MOTOR_DIR_FORWARD);
    return hal_motor_cmd_ok(r) ? SW_OK : SW_ERR_STATE;
}

sw_err_t fan_stop(void)
{
    if (s_exec == NULL) {
        return SW_ERR_NOT_INIT;
    }
    (void)hal_motor_stop(s_exec, s_motor);
    return SW_OK;
}

fan_state_t fan_state(void)
{
    hal_motor_phase_t ph;

    if (s_exec == NULL) {
        return FAN_STATE_IDLE;
    }

    ph = hal_motor_phase(s_exec, s_motor);
    switch (ph) {
    case HAL_MOTOR_PHASE_STOPPED:
    case HAL_MOTOR_PHASE_WAITING_START:
    case HAL_MOTOR_PHASE_PAUSED:
        return FAN_STATE_IDLE;

    case HAL_MOTOR_PHASE_RUNNING:
        return FAN_STATE_RUNNING;

    case HAL_MOTOR_PHASE_DECELERATING:
    case HAL_MOTOR_PHASE_REVERSAL_WAIT:
        return FAN_STATE_STOPPING;

    case HAL_MOTOR_PHASE_FAULT:
    case HAL_MOTOR_PHASE_ESTOP:
        return FAN_STATE_FAULT;

    default:
        return FAN_STATE_IDLE;
    }
}

hal_motor_fault_code_t fan_fault_code(void)
{
    if (s_exec == NULL) {
        return HAL_MOTOR_FAULT_NONE;
    }
    return hal_motor_fault_code(s_exec, s_motor);
}

sw_err_t fan_recover(hal_motor_recovery_step_t step)
{
    if (s_exec == NULL) {
        return SW_ERR_NOT_INIT;
    }
    (void)hal_motor_recover(s_exec, s_motor, step);
    return SW_OK;
}
