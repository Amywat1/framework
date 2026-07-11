/**
 * @file    m8_fan.c
 * @brief   M8 风机机构（组合 continuous_rotary 模式）
 * @author  HUWANGWEI
 * @date    2026-07-11
 */

#include "projects/m8/domain/mechanism/m8_fan.h"
#include "framework/domain/device_control/patterns/continuous_rotary.h"

static continuous_rotary_t s_rotary;

static fan_state_t map_state(void)
{
    switch (continuous_rotary_state(&s_rotary))
    {
    case CONTINUOUS_ROTARY_STATE_RUNNING:
        return FAN_STATE_RUNNING;

    case CONTINUOUS_ROTARY_STATE_STOPPING:
        return FAN_STATE_STOPPING;

    case CONTINUOUS_ROTARY_STATE_FAULT:
        return FAN_STATE_FAULT;

    default:
        return FAN_STATE_IDLE;
    }
}

sw_err_t fan_init(hal_motor_exec_t *exec, int motor, const motion_lifecycle_opts_t *opts)
{
    return continuous_rotary_init(&s_rotary, exec, motor, opts);
}

sw_err_t fan_start(void)
{
    return continuous_rotary_start(&s_rotary, 0);
}

sw_err_t fan_stop(void)
{
    return continuous_rotary_stop(&s_rotary);
}

fan_state_t fan_state(void)
{
    return map_state();
}

hal_motor_fault_code_t fan_fault_code(void)
{
    return continuous_rotary_fault_code(&s_rotary);
}

sw_err_t fan_recover(hal_motor_recovery_step_t step)
{
    return continuous_rotary_recover(&s_rotary, step);
}
