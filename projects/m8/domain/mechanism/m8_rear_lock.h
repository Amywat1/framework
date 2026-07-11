/**
 * @file    m8_rear_lock.h
 * @brief   M8 后轮锁止机构接口
 * @author  HUWANGWEI
 * @date    2026-07-11
 */

#ifndef M8_DOMAIN_MECHANISM_REAR_LOCK_H
#define M8_DOMAIN_MECHANISM_REAR_LOCK_H

#include "framework/domain/device_control/patterns/motion_lifecycle.h"
#include "framework/ports/outbound/hal/motor/hal_motor_exec_port.h"
#include "framework/common/sw_error.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum
{
    REAR_LOCK_STATE_IDLE = 0,
    REAR_LOCK_STATE_LOCKING,
    REAR_LOCK_STATE_RELEASING,
    REAR_LOCK_STATE_STOPPING,
    REAR_LOCK_STATE_FAULT,
} rear_lock_state_t;

sw_err_t rear_lock_init(hal_motor_exec_t *exec, int motor, const motion_lifecycle_opts_t *opts);
sw_err_t rear_lock_lock(int speed_gear, const hal_motor_move_spec_t *spec);
sw_err_t rear_lock_release(int speed_gear, const hal_motor_move_spec_t *spec);
sw_err_t rear_lock_stop(void);
sw_err_t rear_lock_home(void);
rear_lock_state_t rear_lock_state(void);
hal_motor_fault_code_t rear_lock_fault_code(void);
sw_err_t rear_lock_recover(hal_motor_recovery_step_t step);

#ifdef __cplusplus
}
#endif

#endif /* M8_DOMAIN_MECHANISM_REAR_LOCK_H */
