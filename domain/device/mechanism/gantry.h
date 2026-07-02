/**
 * @file    gantry.h
 * @brief   龙门行走机构领域层接口。
 *
 * 封装 motor_executor_t，提供方向感知的行走控制接口。
 * 支持：按限位到位、按时间运行、连续运行、按位置到位（需已建立编码器基准）。
 *
 * 调用方须以固定节拍调用 gantry_tick()（推荐 20ms）。
 * 本模块内部驱动 motor_tick()，调用方无需另外调用。
 */
#ifndef DOMAIN_DEVICE_MECHANISM_GANTRY_H
#define DOMAIN_DEVICE_MECHANISM_GANTRY_H

#include "motor/motor_executor.h"
#include "common/sw_error.h"
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ----------------------- 类型定义 ----------------------- */

/**
 * @brief 龙门行走状态。
 *
 * 状态转移：
 *   IDLE ──(gantry_move_fwd/rev)──► MOVING_FWD/MOVING_REV
 *   MOVING_* ──(gantry_stop / 到位)──► STOPPING ──► IDLE
 *   任意态 ──(故障/急停)──► FAULT
 */
typedef enum {
    GANTRY_STATE_IDLE = 0,   /**< 空闲：电机停止 */
    GANTRY_STATE_MOVING_FWD, /**< 前进中（向车辆入口方向） */
    GANTRY_STATE_MOVING_REV, /**< 后退中（回原位方向） */
    GANTRY_STATE_STOPPING,   /**< 减速停止中 */
    GANTRY_STATE_FAULT,      /**< 故障，需调用 gantry_recover() 恢复 */
} gantry_state_t;

/* ----------------------- 公共 API ----------------------- */

/**
 * @brief 初始化龙门模块，注入已完成 motor_init 的执行器。
 *
 * @param exec  已完成 motor_init 的执行器，龙门模块独占 motor 0。
 * @return SW_OK 成功；SW_ERR_PARAM 参数非法。
 */
sw_err_t gantry_init(motor_executor_t *exec);

/**
 * @brief 命令龙门向前运动（异步）。
 *
 * @param speed_gear  速度挡位索引（对应 motor_config_t 中的 gear_freq）。
 * @param spec        运动结束条件，NULL 表示连续运行直到显式停止。
 *                    常用组合：use_limit（按限位到位）、use_time（按时间）。
 * @return SW_OK 命令已受理；SW_ERR_NOT_INIT 模块未初始化；
 *         SW_ERR_PARAM 参数非法；SW_ERR_STATE 当前处于故障态。
 */
sw_err_t gantry_move_fwd(int speed_gear, const motor_move_spec_t *spec);

/**
 * @brief 命令龙门向后运动（异步）。
 *
 * @param speed_gear  速度挡位索引。
 * @param spec        运动结束条件，NULL 表示连续运行直到显式停止。
 * @return SW_OK 命令已受理；SW_ERR_NOT_INIT 模块未初始化；
 *         SW_ERR_PARAM 参数非法；SW_ERR_STATE 当前处于故障态。
 */
sw_err_t gantry_move_rev(int speed_gear, const motor_move_spec_t *spec);

/**
 * @brief 命令龙门减速停止（异步）。
 *
 * @return SW_OK 命令已受理；SW_ERR_NOT_INIT 模块未初始化。
 */
sw_err_t gantry_stop(void);

/**
 * @brief 命令龙门回原点（向后运动至后限位，异步）。
 *
 * 触发后限位时建立编码器基准（baseline_trusted=true），
 * 完成后可使用按位置到位功能。
 *
 * @return SW_OK 命令已受理；SW_ERR_NOT_INIT 模块未初始化；
 *         SW_ERR_STATE 当前处于故障态。
 */
sw_err_t gantry_home(void);

/**
 * @brief 龙门模块周期处理，须以固定节拍调用（推荐 20ms）。
 *
 * 内部同时驱动 motor_tick()，调用方无需单独调用。
 */
void gantry_tick(void);

/**
 * @brief 查询龙门当前状态。
 * @return 当前 gantry_state_t。
 */
gantry_state_t gantry_state(void);

/**
 * @brief 查询龙门当前编码器累计位置（脉冲）。
 *
 * @note 仅当 gantry_home() 完成后 baseline_trusted 为 true 时，
 *       该值才具有绝对位置意义。
 * @return 累计脉冲计数（负值表示反向累计）。
 */
int64_t gantry_position(void);

/**
 * @brief 查询底层电机故障码（仅在 GANTRY_STATE_FAULT 时有实质意义）。
 * @return 当前 motor_fault_code_t。
 */
motor_fault_code_t gantry_fault_code(void);

/**
 * @brief 三步故障恢复（须在 GANTRY_STATE_FAULT 时调用）。
 *
 * 步骤：
 *   1. 调用 MOTOR_RECOVERY_DRIVER_RESET（驱动器复位）；
 *   2. 调用 MOTOR_RECOVERY_MODULE_STOP（模块停止，之后可重新运动）。
 *
 * @param step 恢复步骤。
 * @return SW_OK 步骤已执行；SW_ERR_NOT_INIT 模块未初始化。
 */
sw_err_t gantry_recover(motor_recovery_step_t step);

#ifdef __cplusplus
}
#endif

#endif /* DOMAIN_DEVICE_MECHANISM_GANTRY_H */
