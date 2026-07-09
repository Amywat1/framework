/**
 * @file    gantry.c
 * @brief   龙门行走机构领域层实现
 * @author  HUWANGWEI
 * @date    2026-07-09
 */

#include "framework/domain/device_control/mechanism/gantry.h"
#include "framework/domain/device_control/mechanism/gantry_alarm_codes.h"
#include "framework/domain/safety/model/alarm_types.h"
#include "framework/ports/inbound/safety/alarm_binding_port.h"
#include "framework/ports/outbound/hal/motor/hal_motor_exec_port.h"
#include <stddef.h>

static hal_motor_exec_t *s_exec;
static int               s_motor;

static void gantry_reevaluate_scope(void)
{
    const alarm_binding_ops_t *ops = alarm_binding_get_ops();

    if ((ops != NULL) && (ops->reevaluate_by_scope != NULL))
    {
        (void)ops->reevaluate_by_scope(ALARM_SCOPE_GANTRY);
    }
}

static void gantry_trigger_motion_fault(bool is_fwd)
{
    const alarm_binding_ops_t *ops = alarm_binding_get_ops();
    hal_motor_fault_code_t     fault;
    uint32_t                   code;

    if (ops == NULL)
    {
        return;
    }

    fault = gantry_fault_code();
    if ((fault == HAL_MOTOR_FAULT_ENCODER_SIGNAL) ||
        (fault == HAL_MOTOR_FAULT_WATCHDOG))
    {
        code = GANTRY_ALM_ENC_ERR;
    }
    else if (is_fwd)
    {
        code = GANTRY_ALM_FWD_TMO;
    }
    else
    {
        code = GANTRY_ALM_REV_TMO;
    }

    if (ops->trigger != NULL)
    {
        (void)ops->trigger(code);
    }
}

sw_err_t gantry_init(hal_motor_exec_t *exec, int motor)
{
    if (exec == NULL)
    {
        return SW_ERR_PARAM;
    }
    s_exec  = exec;
    s_motor = motor;
    return SW_OK;
}

sw_err_t gantry_move_fwd(int speed_gear, const hal_motor_move_spec_t *spec)
{
    hal_motor_cmd_result_t r;
    sw_err_t               ret;

    if (s_exec == NULL)
    {
        return SW_ERR_NOT_INIT;
    }
    if (gantry_state() == GANTRY_STATE_FAULT)
    {
        return SW_ERR_STATE;
    }

    if (spec == NULL)
    {
        r = hal_motor_run_continuous(s_exec, s_motor, hal_motor_speed_gear(speed_gear),
                                     HAL_MOTOR_DIR_FORWARD);
    }
    else
    {
        r = hal_motor_move_to(s_exec, s_motor, hal_motor_speed_gear(speed_gear),
                              HAL_MOTOR_DIR_FORWARD, spec);
    }

    ret = hal_motor_cmd_ok(r) ? SW_OK : SW_ERR_STATE;
    if (ret != SW_OK)
    {
        gantry_trigger_motion_fault(true);
    }
    return ret;
}

sw_err_t gantry_move_rev(int speed_gear, const hal_motor_move_spec_t *spec)
{
    hal_motor_cmd_result_t r;
    sw_err_t               ret;

    if (s_exec == NULL)
    {
        return SW_ERR_NOT_INIT;
    }
    if (gantry_state() == GANTRY_STATE_FAULT)
    {
        return SW_ERR_STATE;
    }

    if (spec == NULL)
    {
        r = hal_motor_run_continuous(s_exec, s_motor, hal_motor_speed_gear(speed_gear),
                                     HAL_MOTOR_DIR_REVERSE);
    }
    else
    {
        r = hal_motor_move_to(s_exec, s_motor, hal_motor_speed_gear(speed_gear),
                              HAL_MOTOR_DIR_REVERSE, spec);
    }

    ret = hal_motor_cmd_ok(r) ? SW_OK : SW_ERR_STATE;
    if (ret != SW_OK)
    {
        gantry_trigger_motion_fault(false);
    }
    return ret;
}

sw_err_t gantry_stop(void)
{
    hal_motor_cmd_result_t r;

    if (s_exec == NULL)
    {
        return SW_ERR_NOT_INIT;
    }

    r = hal_motor_stop(s_exec, s_motor);
    if (hal_motor_cmd_ok(r))
    {
        gantry_reevaluate_scope();
    }
    return hal_motor_cmd_ok(r) ? SW_OK : SW_ERR_STATE;
}

sw_err_t gantry_home(void)
{
    hal_motor_cmd_result_t r;

    if (s_exec == NULL)
    {
        return SW_ERR_NOT_INIT;
    }
    if (gantry_state() == GANTRY_STATE_FAULT)
    {
        return SW_ERR_STATE;
    }

    r = hal_motor_home(s_exec, s_motor);
    return hal_motor_cmd_ok(r) ? SW_OK : SW_ERR_STATE;
}

gantry_state_t gantry_state(void)
{
    hal_motor_phase_t ph;

    if (s_exec == NULL)
    {
        return GANTRY_STATE_IDLE;
    }

    ph = hal_motor_phase(s_exec, s_motor);
    switch (ph)
    {
    case HAL_MOTOR_PHASE_STOPPED:
    case HAL_MOTOR_PHASE_WAITING_START:
    case HAL_MOTOR_PHASE_PAUSED:
        return GANTRY_STATE_IDLE;

    case HAL_MOTOR_PHASE_RUNNING:
        return (hal_motor_direction(s_exec, s_motor) == HAL_MOTOR_DIR_FORWARD)
               ? GANTRY_STATE_MOVING_FWD
               : GANTRY_STATE_MOVING_REV;

    case HAL_MOTOR_PHASE_DECELERATING:
    case HAL_MOTOR_PHASE_REVERSAL_WAIT:
        return GANTRY_STATE_STOPPING;

    case HAL_MOTOR_PHASE_FAULT:
    case HAL_MOTOR_PHASE_ESTOP:
        return GANTRY_STATE_FAULT;

    default:
        return GANTRY_STATE_IDLE;
    }
}

int64_t gantry_position(void)
{
    if (s_exec == NULL)
    {
        return 0;
    }
    return hal_motor_position(s_exec, s_motor);
}

hal_motor_fault_code_t gantry_fault_code(void)
{
    if (s_exec == NULL)
    {
        return HAL_MOTOR_FAULT_NONE;
    }
    return hal_motor_fault_code(s_exec, s_motor);
}

sw_err_t gantry_recover(hal_motor_recovery_step_t step)
{
    hal_motor_cmd_result_t r;

    if (s_exec == NULL)
    {
        return SW_ERR_NOT_INIT;
    }

    r = hal_motor_recover(s_exec, s_motor, step);
    if (hal_motor_cmd_ok(r))
    {
        gantry_reevaluate_scope();
    }
    return hal_motor_cmd_ok(r) ? SW_OK : SW_ERR_STATE;
}
