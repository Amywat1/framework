/**
 * @file    motor_exec_port.c
 * @brief   电机执行器出站端口的每实例分派。
 */

#include "domain/ports/outbound/motor/motor_exec_provider.h"

#include <stddef.h>

static motor_cmd_result_t unavailable(void)
{
    motor_cmd_result_t result;

    result.status = MOTOR_CMD_REJECTED;
    result.reason = "executor-unavailable";
    return result;
}

static bool ops_valid(const motor_exec_ops_t *ops)
{
    return (ops != NULL) && (ops->run != NULL) && (ops->stop != NULL) && (ops->home != NULL) && (ops->recover != NULL)
           && (ops->phase != NULL) && (ops->position != NULL) && (ops->direction != NULL) && (ops->fault_code != NULL)
           && (ops->encoder_healthy != NULL) && (ops->baseline_trusted != NULL) && (ops->pop_event != NULL)
           && (ops->pop_event_for != NULL);
}

static bool exec_valid(const motor_exec_t *exec)
{
    return (exec != NULL) && (exec->ctx != NULL) && ops_valid(exec->ops);
}

bool motor_exec_provider_bind(motor_exec_t *exec, const motor_exec_ops_t *ops, void *ctx)
{
    if ((exec == NULL) || (ctx == NULL) || !ops_valid(ops)) {
        return false;
    }
    exec->ops = ops;
    exec->ctx = ctx;
    return true;
}

motor_cmd_result_t motor_exec_run(motor_exec_t            *exec,
                                     int                          motor,
                                     motor_speed_t            speed,
                                     motor_dir_t              dir,
                                     const motor_move_spec_t *spec)
{
    return exec_valid(exec) ? exec->ops->run(exec->ctx, motor, speed, dir, spec) : unavailable();
}

motor_cmd_result_t motor_exec_stop(motor_exec_t *exec, int motor)
{
    return exec_valid(exec) ? exec->ops->stop(exec->ctx, motor) : unavailable();
}

motor_cmd_result_t motor_exec_home(motor_exec_t *exec, int motor)
{
    return exec_valid(exec) ? exec->ops->home(exec->ctx, motor) : unavailable();
}

motor_cmd_result_t motor_exec_recover(motor_exec_t *exec, int motor, motor_exec_recovery_step_t step)
{
    return exec_valid(exec) ? exec->ops->recover(exec->ctx, motor, step) : unavailable();
}

motor_exec_phase_t motor_exec_phase(const motor_exec_t *exec, int motor)
{
    return exec_valid(exec) ? exec->ops->phase(exec->ctx, motor) : MOTOR_PHASE_STOPPED;
}

int64_t motor_exec_position(const motor_exec_t *exec, int motor)
{
    return exec_valid(exec) ? exec->ops->position(exec->ctx, motor) : 0;
}

motor_dir_t motor_exec_direction(const motor_exec_t *exec, int motor)
{
    return exec_valid(exec) ? exec->ops->direction(exec->ctx, motor) : MOTOR_DIR_FORWARD;
}

motor_exec_fault_code_t motor_exec_fault_code(const motor_exec_t *exec, int motor)
{
    return exec_valid(exec) ? exec->ops->fault_code(exec->ctx, motor) : MOTOR_FAULT_DRIVER_PORT_FATAL;
}

bool motor_exec_encoder_healthy(const motor_exec_t *exec, int motor)
{
    return exec_valid(exec) && exec->ops->encoder_healthy(exec->ctx, motor);
}

bool motor_exec_baseline_trusted(const motor_exec_t *exec, int motor)
{
    return exec_valid(exec) && exec->ops->baseline_trusted(exec->ctx, motor);
}

bool motor_exec_pop_event(motor_exec_t *exec, motor_event_t *out)
{
    return exec_valid(exec) && (out != NULL) && exec->ops->pop_event(exec->ctx, out);
}

bool motor_exec_pop_event_for(motor_exec_t *exec, int motor, motor_event_t *out)
{
    return exec_valid(exec) && (out != NULL) && exec->ops->pop_event_for(exec->ctx, motor, out);
}
