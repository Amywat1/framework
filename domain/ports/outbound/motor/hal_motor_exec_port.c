/**
 * @file    hal_motor_exec_port.c
 * @brief   电机执行器出站端口的每实例分派。
 */

#include "domain/ports/outbound/motor/hal_motor_exec_provider.h"

#include <stddef.h>

static hal_motor_cmd_result_t unavailable(void)
{
    hal_motor_cmd_result_t result;

    result.status = HAL_MOTOR_CMD_REJECTED;
    result.reason = "executor-unavailable";
    return result;
}

static bool ops_valid(const hal_motor_exec_ops_t *ops)
{
    return (ops != NULL) && (ops->run != NULL) && (ops->stop != NULL) && (ops->home != NULL) && (ops->recover != NULL)
           && (ops->phase != NULL) && (ops->position != NULL) && (ops->direction != NULL) && (ops->fault_code != NULL)
           && (ops->encoder_healthy != NULL) && (ops->baseline_trusted != NULL) && (ops->pop_event != NULL)
           && (ops->pop_event_for != NULL);
}

static bool exec_valid(const hal_motor_exec_t *exec)
{
    return (exec != NULL) && (exec->ctx != NULL) && ops_valid(exec->ops);
}

bool hal_motor_exec_provider_bind(hal_motor_exec_t *exec, const hal_motor_exec_ops_t *ops, void *ctx)
{
    if ((exec == NULL) || (ctx == NULL) || !ops_valid(ops)) {
        return false;
    }
    exec->ops = ops;
    exec->ctx = ctx;
    return true;
}

hal_motor_cmd_result_t hal_motor_run(hal_motor_exec_t            *exec,
                                     int                          motor,
                                     hal_motor_speed_t            speed,
                                     hal_motor_dir_t              dir,
                                     const hal_motor_move_spec_t *spec)
{
    return exec_valid(exec) ? exec->ops->run(exec->ctx, motor, speed, dir, spec) : unavailable();
}

hal_motor_cmd_result_t hal_motor_stop(hal_motor_exec_t *exec, int motor)
{
    return exec_valid(exec) ? exec->ops->stop(exec->ctx, motor) : unavailable();
}

hal_motor_cmd_result_t hal_motor_home(hal_motor_exec_t *exec, int motor)
{
    return exec_valid(exec) ? exec->ops->home(exec->ctx, motor) : unavailable();
}

hal_motor_cmd_result_t hal_motor_recover(hal_motor_exec_t *exec, int motor, hal_motor_recovery_step_t step)
{
    return exec_valid(exec) ? exec->ops->recover(exec->ctx, motor, step) : unavailable();
}

hal_motor_phase_t hal_motor_phase(const hal_motor_exec_t *exec, int motor)
{
    return exec_valid(exec) ? exec->ops->phase(exec->ctx, motor) : HAL_MOTOR_PHASE_STOPPED;
}

int64_t hal_motor_position(const hal_motor_exec_t *exec, int motor)
{
    return exec_valid(exec) ? exec->ops->position(exec->ctx, motor) : 0;
}

hal_motor_dir_t hal_motor_direction(const hal_motor_exec_t *exec, int motor)
{
    return exec_valid(exec) ? exec->ops->direction(exec->ctx, motor) : HAL_MOTOR_DIR_FORWARD;
}

hal_motor_fault_code_t hal_motor_fault_code(const hal_motor_exec_t *exec, int motor)
{
    return exec_valid(exec) ? exec->ops->fault_code(exec->ctx, motor) : HAL_MOTOR_FAULT_DRIVER_PORT_FATAL;
}

bool hal_motor_encoder_healthy(const hal_motor_exec_t *exec, int motor)
{
    return exec_valid(exec) && exec->ops->encoder_healthy(exec->ctx, motor);
}

bool hal_motor_baseline_trusted(const hal_motor_exec_t *exec, int motor)
{
    return exec_valid(exec) && exec->ops->baseline_trusted(exec->ctx, motor);
}

bool hal_motor_pop_event(hal_motor_exec_t *exec, hal_motor_event_t *out)
{
    return exec_valid(exec) && (out != NULL) && exec->ops->pop_event(exec->ctx, out);
}

bool hal_motor_pop_event_for(hal_motor_exec_t *exec, int motor, hal_motor_event_t *out)
{
    return exec_valid(exec) && (out != NULL) && exec->ops->pop_event_for(exec->ctx, motor, out);
}
