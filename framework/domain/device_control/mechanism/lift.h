/**
 * @file    lift.h
 * @brief   顶刷升降机构领域层接口。
 *
 * 封装 motor_executor_t，提供方向感知的升降控制接口。
 * 支持：按限位到位、按时间运行、连续运行、回原点。
 *
 * motor_tick() 由机型层统一调度，调用方无需另行调用。
 */
#ifndef DOMAIN_DEVICE_MECHANISM_LIFT_H
#define DOMAIN_DEVICE_MECHANISM_LIFT_H

#include "framework/ports/outbound/hal/motor/hal_motor_exec_port.h"
#include "framework/common/sw_error.h"
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ----------------------- 类型定义 ----------------------- */

/**
 * @brief 升降机构状态。
 *
 * 状态转移：
 *   IDLE ──(lift_up/lift_down)──► LIFTING/LOWERING
 *   LIFTING/LOWERING ──(到位/lift_stop)──► STOPPING ──► IDLE
 *   任意态 ──(故障/急停)──► FAULT
 */
typedef enum {
    LIFT_STATE_IDLE = 0, /**< 空闲：电机停止 */
    LIFT_STATE_LIFTING,  /**< 上升中 */
    LIFT_STATE_LOWERING, /**< 下降中 */
    LIFT_STATE_STOPPING, /**< 减速停止中 */
    LIFT_STATE_FAULT,    /**< 故障，需调用 lift_recover() 恢复 */
} lift_state_t;

/* ----------------------- 公共 API ----------------------- */

/**
 * @brief 初始化升降模块，注入执行器及所在电机索引。
 *
 * @param exec   共享 motor_executor_t，须已完成 motor_init。
 * @param motor  本模块对应的电机索引（由机型层分配）。
 * @return SW_OK 成功；SW_ERR_PARAM 参数非法。
 */
sw_err_t lift_init(hal_motor_exec_t *exec, int motor);

/**
 * @brief 命令升降机构上升（异步）。
 *
 * @param speed_gear  速度挡位索引（对应 motor_config_t 中的 gear_freq）。
 * @param spec        运动结束条件，NULL 表示连续运行直到显式停止。
 * @return SW_OK 命令已受理；SW_ERR_NOT_INIT 模块未初始化；
 *         SW_ERR_PARAM 参数非法；SW_ERR_STATE 当前处于故障态。
 */
sw_err_t lift_up(int speed_gear, const hal_motor_move_spec_t *spec);

/**
 * @brief 命令升降机构下降（异步）。
 *
 * @param speed_gear  速度挡位索引。
 * @param spec        运动结束条件，NULL 表示连续运行直到显式停止。
 * @return SW_OK 命令已受理；SW_ERR_NOT_INIT 模块未初始化；
 *         SW_ERR_PARAM 参数非法；SW_ERR_STATE 当前处于故障态。
 */
sw_err_t lift_down(int speed_gear, const hal_motor_move_spec_t *spec);

/**
 * @brief 命令升降机构减速停止（异步）。
 *
 * @return SW_OK 命令已受理；SW_ERR_NOT_INIT 模块未初始化。
 */
sw_err_t lift_stop(void);

/**
 * @brief 命令升降机构回原点（向下运动至下限位，建立编码器基准，异步）。
 *
 * @return SW_OK 命令已受理；SW_ERR_NOT_INIT 模块未初始化；
 *         SW_ERR_STATE 当前处于故障态。
 */
sw_err_t lift_home(void);

/**
 * @brief 查询升降机构当前状态。
 * @return 当前 lift_state_t。
 */
lift_state_t lift_state(void);

/**
 * @brief 查询升降机构当前编码器累计位置（脉冲）。
 *
 * @note 仅当 lift_home() 完成后 baseline_trusted 为 true 时，
 *       该值才具有绝对位置意义。
 * @return 累计脉冲计数。
 */
int64_t lift_position(void);

/**
 * @brief 查询底层电机故障码（仅在 LIFT_STATE_FAULT 时有实质意义）。
 * @return 当前 motor_fault_code_t。
 */
hal_motor_fault_code_t lift_fault_code(void);

/**
 * @brief 三步故障恢复（须在 LIFT_STATE_FAULT 时调用）。
 *
 * @param step 恢复步骤。
 * @return SW_OK 步骤已执行；SW_ERR_NOT_INIT 模块未初始化。
 */
sw_err_t lift_recover(hal_motor_recovery_step_t step);

#ifdef __cplusplus
}
#endif

#endif /* DOMAIN_DEVICE_MECHANISM_LIFT_H */
