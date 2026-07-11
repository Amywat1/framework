/**
 * @file    gantry.c
 * @brief   M8 龙门行走机构（组合 bidirectional_motion 模式）
 * @author  HUWANGWEI
 * @date    2026-07-09
 */

#include "projects/m8/domain/mechanism/gantry.h"
#include "framework/domain/device_control/patterns/bidirectional_motion.h"
#include <stddef.h>

static bidirectional_motion_t s_motion;

static gantry_state_t map_state(bidir_motion_state_t st, hal_motor_dir_t dir)
{
    switch (st)
    {
    case BIDIR_MOTION_STATE_MOVING:
        return (dir == HAL_MOTOR_DIR_FORWARD) ? GANTRY_STATE_MOVING_FWD : GANTRY_STATE_MOVING_REV;

    case BIDIR_MOTION_STATE_STOPPING:
        return GANTRY_STATE_STOPPING;

    case BIDIR_MOTION_STATE_FAULT:
        return GANTRY_STATE_FAULT;

    default:
        return GANTRY_STATE_IDLE;
    }
}

sw_err_t gantry_init(hal_motor_exec_t *exec, int motor, const gantry_options_t *opts)
{
    motion_lifecycle_opts_t lc = { 0 };

    if (opts != NULL)
    {
        lc.motion_actuator_id = opts->motion_actuator_id;
        lc.on_process_fault   = opts->on_process_fault;
    }
    return bidirectional_motion_init(&s_motion, exec, motor, &lc);
}

sw_err_t gantry_move_fwd(int speed_gear, const hal_motor_move_spec_t *spec)
{
    return bidirectional_motion_run(&s_motion, HAL_MOTOR_DIR_FORWARD, speed_gear, spec);
}

sw_err_t gantry_move_rev(int speed_gear, const hal_motor_move_spec_t *spec)
{
    return bidirectional_motion_run(&s_motion, HAL_MOTOR_DIR_REVERSE, speed_gear, spec);
}

sw_err_t gantry_stop(void)
{
    return bidirectional_motion_stop(&s_motion);
}

sw_err_t gantry_home(void)
{
    return bidirectional_motion_home(&s_motion);
}

gantry_state_t gantry_state(void)
{
    return map_state(bidirectional_motion_state(&s_motion),
                     bidirectional_motion_direction(&s_motion));
}

int64_t gantry_position(void)
{
    return bidirectional_motion_position(&s_motion);
}

hal_motor_fault_code_t gantry_fault_code(void)
{
    return bidirectional_motion_fault_code(&s_motion);
}

sw_err_t gantry_recover(hal_motor_recovery_step_t step)
{
    return bidirectional_motion_recover(&s_motion, step);
}
