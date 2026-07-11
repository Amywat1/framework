/**
 * @file    m8_fan.h
 * @brief   M8 风机机构接口
 * @author  HUWANGWEI
 * @date    2026-07-11
 */

#ifndef M8_DOMAIN_MECHANISM_FAN_H
#define M8_DOMAIN_MECHANISM_FAN_H

#include "framework/domain/device_control/patterns/motion_lifecycle.h"
#include "framework/ports/outbound/hal/motor/hal_motor_exec_port.h"
#include "framework/common/sw_error.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum
{
    FAN_STATE_IDLE = 0,
    FAN_STATE_RUNNING,
    FAN_STATE_STOPPING,
    FAN_STATE_FAULT,
} fan_state_t;

sw_err_t fan_init(hal_motor_exec_t *exec, int motor, const motion_lifecycle_opts_t *opts);
sw_err_t fan_start(void);
sw_err_t fan_stop(void);
fan_state_t fan_state(void);
hal_motor_fault_code_t fan_fault_code(void);
sw_err_t fan_recover(hal_motor_recovery_step_t step);

#ifdef __cplusplus
}
#endif

#endif /* M8_DOMAIN_MECHANISM_FAN_H */
