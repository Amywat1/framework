/**
 * @file    fan.h
 * @brief   风机机构领域层接口。
 *
 * 封装 motor_executor_t，提供风机启停控制接口。风机仅正转、无编码器、
 * 无限位，故障检测与恢复完全由 MCC 执行器负责。
 *
 * motor_tick() 由机型层统一调度，调用方无需另行调用。
 */
#ifndef DOMAIN_DEVICE_MECHANISM_FAN_H
#define DOMAIN_DEVICE_MECHANISM_FAN_H

#include "motor/motor_executor.h"
#include "common/sw_error.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ----------------------- 类型定义 ----------------------- */

/**
 * @brief 风机状态。
 *
 * 状态转移：
 *   IDLE ──(fan_start)──► RUNNING
 *   RUNNING ──(fan_stop)──► STOPPING ──► IDLE
 *   任意态 ──(故障/急停)──► FAULT
 */
typedef enum {
    FAN_STATE_IDLE = 0, /**< 空闲：电机停止 */
    FAN_STATE_RUNNING,  /**< 运行中 */
    FAN_STATE_STOPPING, /**< 减速停止中 */
    FAN_STATE_FAULT,    /**< 故障，需调用 fan_recover() 恢复 */
} fan_state_t;

/* ----------------------- 公共 API ----------------------- */

/**
 * @brief 初始化风机模块，注入执行器及所在电机索引。
 *
 * @param exec   共享 motor_executor_t，须已完成 motor_init。
 * @param motor  本模块对应的电机索引（由机型层分配）。
 * @return SW_OK 成功；SW_ERR_PARAM 参数非法。
 */
sw_err_t fan_init(motor_executor_t *exec, int motor);

/**
 * @brief 启动风机（连续运行，异步）。
 *
 * @return SW_OK 命令已受理；SW_ERR_NOT_INIT 模块未初始化；
 *         SW_ERR_STATE 当前处于故障态。
 */
sw_err_t fan_start(void);

/**
 * @brief 停止风机（减速停止，异步）。
 *
 * @return SW_OK 命令已受理；SW_ERR_NOT_INIT 模块未初始化。
 */
sw_err_t fan_stop(void);

/**
 * @brief 查询风机当前状态。
 * @return 当前 fan_state_t。
 */
fan_state_t fan_state(void);

/**
 * @brief 查询底层电机故障码（仅在 FAN_STATE_FAULT 时有实质意义）。
 * @return 当前 motor_fault_code_t。
 */
motor_fault_code_t fan_fault_code(void);

/**
 * @brief 三步故障恢复（须在 FAN_STATE_FAULT 时调用）。
 *
 * @param step 恢复步骤。
 * @return SW_OK 步骤已执行；SW_ERR_NOT_INIT 模块未初始化。
 */
sw_err_t fan_recover(motor_recovery_step_t step);

#ifdef __cplusplus
}
#endif

#endif /* DOMAIN_DEVICE_MECHANISM_FAN_H */
