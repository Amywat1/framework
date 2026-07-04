/**
 * @file    rear_lock.h
 * @brief   后轮锁止机构领域层接口。
 *
 * 封装 motor_executor_t，提供锁止/释放语义的推杆控制接口。
 * 支持：按限位到位、连续运行、回原点（释放到位建立编码器基准）。
 *
 * motor_tick() 由机型层统一调度，调用方无需另行调用。
 */
#ifndef DOMAIN_DEVICE_MECHANISM_REAR_LOCK_H
#define DOMAIN_DEVICE_MECHANISM_REAR_LOCK_H

#include "framework/ports/outbound/hal/motor/hal_motor_exec_port.h"
#include "framework/common/sw_error.h"
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ----------------------- 类型定义 ----------------------- */

/**
 * @brief 后轮锁止机构状态。
 *
 * 状态转移：
 *   IDLE ──(rear_lock_lock)──► LOCKING ──(到位/rear_lock_stop)──► STOPPING ──► IDLE
 *   IDLE ──(rear_lock_release)──► RELEASING ──(到位/rear_lock_stop)──► STOPPING ──► IDLE
 *   任意态 ──(故障/急停)──► FAULT
 */
typedef enum {
    REAR_LOCK_STATE_IDLE = 0,  /**< 空闲：电机停止 */
    REAR_LOCK_STATE_LOCKING,   /**< 锁止中（推杆伸出） */
    REAR_LOCK_STATE_RELEASING, /**< 释放中（推杆缩回） */
    REAR_LOCK_STATE_STOPPING,  /**< 减速停止中 */
    REAR_LOCK_STATE_FAULT,     /**< 故障，需调用 rear_lock_recover() 恢复 */
} rear_lock_state_t;

/* ----------------------- 公共 API ----------------------- */

/**
 * @brief 初始化后轮锁止模块，注入执行器及所在电机索引。
 *
 * @param exec   共享 motor_executor_t，须已完成 motor_init。
 * @param motor  本模块对应的电机索引（由机型层分配）。
 * @return SW_OK 成功；SW_ERR_PARAM 参数非法。
 */
sw_err_t rear_lock_init(hal_motor_exec_t *exec, int motor);

/**
 * @brief 命令推杆伸出以锁止后轮（异步）。
 *
 * @param speed_gear  速度挡位索引（对应 motor_config_t 中的 gear_freq）。
 * @param spec        运动结束条件，NULL 表示连续运行直到显式停止。
 * @return SW_OK 命令已受理；SW_ERR_NOT_INIT 模块未初始化；
 *         SW_ERR_PARAM 参数非法；SW_ERR_STATE 当前处于故障态。
 */
sw_err_t rear_lock_lock(int speed_gear, const hal_motor_move_spec_t *spec);

/**
 * @brief 命令推杆缩回以释放后轮（异步）。
 *
 * @param speed_gear  速度挡位索引。
 * @param spec        运动结束条件，NULL 表示连续运行直到显式停止。
 * @return SW_OK 命令已受理；SW_ERR_NOT_INIT 模块未初始化；
 *         SW_ERR_PARAM 参数非法；SW_ERR_STATE 当前处于故障态。
 */
sw_err_t rear_lock_release(int speed_gear, const hal_motor_move_spec_t *spec);

/**
 * @brief 命令推杆减速停止（异步）。
 *
 * @return SW_OK 命令已受理；SW_ERR_NOT_INIT 模块未初始化。
 */
sw_err_t rear_lock_stop(void);

/**
 * @brief 命令推杆回原点（缩回至原点传感器触发，建立编码器基准，异步）。
 *
 * @return SW_OK 命令已受理；SW_ERR_NOT_INIT 模块未初始化；
 *         SW_ERR_STATE 当前处于故障态。
 */
sw_err_t rear_lock_home(void);

/**
 * @brief 查询后轮锁止机构当前状态。
 * @return 当前 rear_lock_state_t。
 */
rear_lock_state_t rear_lock_state(void);

/**
 * @brief 查询底层电机故障码（仅在 REAR_LOCK_STATE_FAULT 时有实质意义）。
 * @return 当前 motor_fault_code_t。
 */
hal_motor_fault_code_t rear_lock_fault_code(void);

/**
 * @brief 三步故障恢复（须在 REAR_LOCK_STATE_FAULT 时调用）。
 *
 * @param step 恢复步骤。
 * @return SW_OK 步骤已执行；SW_ERR_NOT_INIT 模块未初始化。
 */
sw_err_t rear_lock_recover(hal_motor_recovery_step_t step);

#ifdef __cplusplus
}
#endif

#endif /* DOMAIN_DEVICE_MECHANISM_REAR_LOCK_H */
