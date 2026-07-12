/**
 * @file    hal_motor_exec_adapter.c
 * @brief   电机执行器出站端口的 MCC provider 适配器。
 *
 * 本文件是框架内唯一直接 #include MCC 头文件的位置；domain/ 与其余
 * 框架代码只通过 hal_motor_exec_port.h 的不透明句柄与镜像类型访问电机。
 */

#include "ports/outbound/hal/motor/hal_motor_exec_port.h"
#include "motor/motor_executor.h"

static motor_direction_t to_mcc_dir(hal_motor_dir_t dir)
{
    return (dir == HAL_MOTOR_DIR_REVERSE) ? MOTOR_DIR_REVERSE : MOTOR_DIR_FORWARD;
}

static hal_motor_dir_t from_mcc_dir(motor_direction_t dir)
{
    return (dir == MOTOR_DIR_REVERSE) ? HAL_MOTOR_DIR_REVERSE : HAL_MOTOR_DIR_FORWARD;
}

static motor_limit_kind_t to_mcc_limit(hal_motor_limit_kind_t kind)
{
    switch (kind) {
    case HAL_MOTOR_LIMIT_NEG:
        return MOTOR_LIMIT_NEG;
    case HAL_MOTOR_LIMIT_ORIGIN:
        return MOTOR_LIMIT_ORIGIN;
    case HAL_MOTOR_LIMIT_POS:
    default:
        return MOTOR_LIMIT_POS;
    }
}

static hal_motor_phase_t from_mcc_phase(motor_phase_t ph)
{
    switch (ph) {
    case MOTOR_PHASE_WAITING_START:
        return HAL_MOTOR_PHASE_WAITING_START;
    case MOTOR_PHASE_REVERSAL_WAIT:
        return HAL_MOTOR_PHASE_REVERSAL_WAIT;
    case MOTOR_PHASE_RUNNING:
        return HAL_MOTOR_PHASE_RUNNING;
    case MOTOR_PHASE_PAUSED:
        return HAL_MOTOR_PHASE_PAUSED;
    case MOTOR_PHASE_DECELERATING:
        return HAL_MOTOR_PHASE_DECELERATING;
    case MOTOR_PHASE_FAULT:
        return HAL_MOTOR_PHASE_FAULT;
    case MOTOR_PHASE_ESTOP:
        return HAL_MOTOR_PHASE_ESTOP;
    case MOTOR_PHASE_STOPPED:
    default:
        return HAL_MOTOR_PHASE_STOPPED;
    }
}

static hal_motor_fault_code_t from_mcc_fault(motor_fault_code_t fc)
{
    switch (fc) {
    case MOTOR_FAULT_OVERCURRENT:
        return HAL_MOTOR_FAULT_OVERCURRENT;
    case MOTOR_FAULT_UNDERCURRENT:
        return HAL_MOTOR_FAULT_UNDERCURRENT;
    case MOTOR_FAULT_DRIVER_FEEDBACK:
        return HAL_MOTOR_FAULT_DRIVER_FEEDBACK;
    case MOTOR_FAULT_OVERTEMP:
        return HAL_MOTOR_FAULT_OVERTEMP;
    case MOTOR_FAULT_UNDERVOLTAGE:
        return HAL_MOTOR_FAULT_UNDERVOLTAGE;
    case MOTOR_FAULT_PREPARE_FAILED:
        return HAL_MOTOR_FAULT_PREPARE_FAILED;
    case MOTOR_FAULT_ENCODER_SIGNAL:
        return HAL_MOTOR_FAULT_ENCODER_SIGNAL;
    case MOTOR_FAULT_WATCHDOG:
        return HAL_MOTOR_FAULT_WATCHDOG;
    case MOTOR_FAULT_DRIVER_PORT_FATAL:
        return HAL_MOTOR_FAULT_DRIVER_PORT_FATAL;
    case MOTOR_FAULT_SHARED_DRIVER:
        return HAL_MOTOR_FAULT_SHARED_DRIVER;
    case MOTOR_FAULT_NONE:
    default:
        return HAL_MOTOR_FAULT_NONE;
    }
}

static motor_recovery_step_t to_mcc_recovery_step(hal_motor_recovery_step_t step)
{
    return (step == HAL_MOTOR_RECOVERY_MODULE_STOP) ? MOTOR_RECOVERY_MODULE_STOP
                                                      : MOTOR_RECOVERY_DRIVER_RESET;
}

static motor_speed_t to_mcc_speed(hal_motor_speed_t spd)
{
    return (spd.kind == HAL_MOTOR_SPEED_GEAR) ? motor_speed_gear(spd.value)
                                               : motor_speed_freq(spd.value);
}

static void to_mcc_move_spec(const hal_motor_move_spec_t *src, motor_move_spec_t *dst)
{
    dst->use_limit = src->use_limit;
    dst->limit = to_mcc_limit(src->limit);
    dst->use_position = src->use_position;
    dst->target_pos = src->target_pos;
    dst->use_soft_limit = src->use_soft_limit;
    dst->use_time = src->use_time;
    dst->duration_ms = src->duration_ms;
    dst->max_time_ms = src->max_time_ms;
}

static hal_motor_cmd_result_t from_mcc_result(motor_cmd_result_t r)
{
    hal_motor_cmd_result_t out;

    switch (r.status) {
    case MOTOR_CMD_QUEUED:
        out.status = HAL_MOTOR_CMD_QUEUED;
        break;
    case MOTOR_CMD_REJECTED:
        out.status = HAL_MOTOR_CMD_REJECTED;
        break;
    case MOTOR_CMD_ACCEPTED:
    default:
        out.status = HAL_MOTOR_CMD_ACCEPTED;
        break;
    }
    out.reason = r.reason;
    return out;
}

hal_motor_cmd_result_t hal_motor_run_continuous(hal_motor_exec_t *exec, int motor,
                                                 hal_motor_speed_t spd, hal_motor_dir_t dir)
{
    return from_mcc_result(motor_run_continuous((motor_executor_t *)exec, motor,
                                                 to_mcc_speed(spd), to_mcc_dir(dir)));
}

hal_motor_cmd_result_t hal_motor_move_to(hal_motor_exec_t *exec, int motor,
                                          hal_motor_speed_t spd, hal_motor_dir_t dir,
                                          const hal_motor_move_spec_t *spec)
{
    motor_move_spec_t mcc_spec;
    const motor_move_spec_t *mcc_spec_p = NULL;

    if (spec != NULL) {
        to_mcc_move_spec(spec, &mcc_spec);
        mcc_spec_p = &mcc_spec;
    }

    return from_mcc_result(motor_move_to((motor_executor_t *)exec, motor,
                                          to_mcc_speed(spd), to_mcc_dir(dir), mcc_spec_p));
}

hal_motor_cmd_result_t hal_motor_stop(hal_motor_exec_t *exec, int motor)
{
    return from_mcc_result(motor_stop((motor_executor_t *)exec, motor));
}

hal_motor_cmd_result_t hal_motor_set_speed(hal_motor_exec_t *exec, int motor,
                                            hal_motor_speed_t spd, hal_motor_dir_t dir)
{
    return from_mcc_result(motor_set_speed((motor_executor_t *)exec, motor,
                                            to_mcc_speed(spd), to_mcc_dir(dir)));
}

hal_motor_cmd_result_t hal_motor_home(hal_motor_exec_t *exec, int motor)
{
    return from_mcc_result(motor_home((motor_executor_t *)exec, motor));
}

hal_motor_cmd_result_t hal_motor_recover(hal_motor_exec_t *exec, int motor,
                                          hal_motor_recovery_step_t step)
{
    return from_mcc_result(motor_recover((motor_executor_t *)exec, motor,
                                          to_mcc_recovery_step(step)));
}

hal_motor_phase_t hal_motor_phase(const hal_motor_exec_t *exec, int motor)
{
    return from_mcc_phase(motor_phase((const motor_executor_t *)exec, motor));
}

int64_t hal_motor_position(const hal_motor_exec_t *exec, int motor)
{
    return motor_position((const motor_executor_t *)exec, motor);
}

hal_motor_dir_t hal_motor_direction(const hal_motor_exec_t *exec, int motor)
{
    return from_mcc_dir(motor_direction((const motor_executor_t *)exec, motor));
}

hal_motor_fault_code_t hal_motor_fault_code(const hal_motor_exec_t *exec, int motor)
{
    return from_mcc_fault(motor_fault_code((const motor_executor_t *)exec, motor));
}
