/**
 * @file    m8_top_brush_lift.c
 * @brief   M8 顶刷升降机构（组合 bidirectional_motion 模式）
 * @author  HUWANGWEI
 * @date    2026-07-11
 */

#include "projects/m8/domain/mechanism/m8_top_brush_lift.h"
#include "framework/domain/device_control/patterns/bidirectional_motion.h"

static bidirectional_motion_t s_motion;

static lift_state_t map_state(void)
{
    bidir_motion_state_t st = bidirectional_motion_state(&s_motion);

    switch (st)
    {
    case BIDIR_MOTION_STATE_MOVING:
        return (bidirectional_motion_direction(&s_motion) == HAL_MOTOR_DIR_FORWARD)
               ? LIFT_STATE_LIFTING : LIFT_STATE_LOWERING;

    case BIDIR_MOTION_STATE_STOPPING:
        return LIFT_STATE_STOPPING;

    case BIDIR_MOTION_STATE_FAULT:
        return LIFT_STATE_FAULT;

    default:
        return LIFT_STATE_IDLE;
    }
}

sw_err_t lift_init(hal_motor_exec_t *exec, int motor, const motion_lifecycle_opts_t *opts)
{
    return bidirectional_motion_init(&s_motion, exec, motor, opts);
}

sw_err_t lift_up(int speed_gear, const hal_motor_move_spec_t *spec)
{
    return bidirectional_motion_run(&s_motion, HAL_MOTOR_DIR_FORWARD, speed_gear, spec);
}

sw_err_t lift_down(int speed_gear, const hal_motor_move_spec_t *spec)
{
    return bidirectional_motion_run(&s_motion, HAL_MOTOR_DIR_REVERSE, speed_gear, spec);
}

sw_err_t lift_stop(void)
{
    return bidirectional_motion_stop(&s_motion);
}

sw_err_t lift_home(void)
{
    return bidirectional_motion_home(&s_motion);
}

lift_state_t lift_state(void)
{
    return map_state();
}

int64_t lift_position(void)
{
    return bidirectional_motion_position(&s_motion);
}

hal_motor_fault_code_t lift_fault_code(void)
{
    return bidirectional_motion_fault_code(&s_motion);
}

sw_err_t lift_recover(hal_motor_recovery_step_t step)
{
    return bidirectional_motion_recover(&s_motion, step);
}
