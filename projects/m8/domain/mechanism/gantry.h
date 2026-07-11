/**
 * @file    gantry.h
 * @brief   M8 龙门行走机构领域层接口
 * @author  HUWANGWEI
 * @date    2026-07-09
 *
 * @note    封装 motor_executor_t，提供方向感知的行走控制接口。
 *          动作完成时发布 lifecycle 事件；流程故障由项目注入回调上报。
 */

#ifndef M8_DOMAIN_MECHANISM_GANTRY_H
#define M8_DOMAIN_MECHANISM_GANTRY_H

#include "framework/domain/device_control/model/actuator_events.h"
#include "framework/ports/outbound/hal/motor/hal_motor_exec_port.h"
#include "framework/common/sw_error.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum
{
    GANTRY_STATE_IDLE = 0,
    GANTRY_STATE_MOVING_FWD,
    GANTRY_STATE_MOVING_REV,
    GANTRY_STATE_STOPPING,
    GANTRY_STATE_FAULT,
} gantry_state_t;

/**
 * @brief  流程类运动故障回调（项目层映射为报警 trigger）
 */
typedef void (*gantry_process_fault_fn_t)(bool is_fwd, hal_motor_fault_code_t fault);

typedef struct
{
    actuator_id_t              motion_actuator_id;
    gantry_process_fault_fn_t  on_process_fault;
} gantry_options_t;

/**
 * @brief  初始化龙门模块
 * @param  exec   共享 motor_executor_t
 * @param  motor  电机索引
 * @param  opts   运行时选项；NULL 表示不发布 lifecycle、不上报流程故障
 */
sw_err_t gantry_init(hal_motor_exec_t *exec, int motor, const gantry_options_t *opts);

sw_err_t gantry_move_fwd(int speed_gear, const hal_motor_move_spec_t *spec);
sw_err_t gantry_move_rev(int speed_gear, const hal_motor_move_spec_t *spec);
sw_err_t gantry_stop(void);
sw_err_t gantry_home(void);
gantry_state_t gantry_state(void);
int64_t gantry_position(void);
hal_motor_fault_code_t gantry_fault_code(void);
sw_err_t gantry_recover(hal_motor_recovery_step_t step);

#ifdef __cplusplus
}
#endif

#endif /* M8_DOMAIN_MECHANISM_GANTRY_H */
