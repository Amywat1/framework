/**
 * @file    m8_motor_exec.h
 * @brief   M8 机型共享电机执行器接口。
 *
 * 全部受 MCC 管理的电机（龙门、刷子、升降、后轮锁止）共享一个
 * motor_executor_t，由 m8_motor_exec_init() 统一完成 motor_init 并注册
 * m8_motor_exec_tick 到电机 tick 管理器。motor_tick 只调用一次，同步
 * 推进全部 4 个轴。
 *
 * 电机索引：
 *   M8_MOTOR_GANTRY    = 0   龙门行走
 *   M8_MOTOR_BRUSH     = 1   刷子（侧刷/顶刷共用 VFD）
 *   M8_MOTOR_LIFT      = 2   顶刷升降
 *   M8_MOTOR_REAR_LOCK = 3   后轮锁止推杆
 *
 * 调用顺序：
 *   m8_motor_exec_init()  → 建立执行器，注册 tick
 *   m8_gantry_setup()     → gantry_init(exec, M8_MOTOR_GANTRY)
 *   m8_brush_setup()      → brush_init(exec, M8_MOTOR_BRUSH, ...) + brush_tick
 *   m8_lift_setup()       → lift_init(exec, M8_MOTOR_LIFT)
 *   m8_rear_lock_setup()  → rear_lock_init(exec, M8_MOTOR_REAR_LOCK)
 *   m8_fan_setup()        → fan_init(...) + fan_tick
 *   m8_motor_tick_start()
 */
#ifndef MACHINES_M8_ADAPTERS_SETUP_M8_MOTOR_EXEC_H
#define MACHINES_M8_ADAPTERS_SETUP_M8_MOTOR_EXEC_H

#include "motor/motor_executor.h"
#include "common/sw_error.h"

#ifdef __cplusplus
extern "C" {
#endif

/** 龙门行走电机索引 */
#define M8_MOTOR_GANTRY     0
/** 刷子（侧刷/顶刷共用 VFD）电机索引 */
#define M8_MOTOR_BRUSH      1
/** 顶刷升降继电器电机索引 */
#define M8_MOTOR_LIFT       2
/** 后轮锁止继电器电机索引 */
#define M8_MOTOR_REAR_LOCK  3

/**
 * @brief 初始化共享 motor_executor_t，配置全部 4 路电机并注册 m8_motor_exec_tick。
 *
 * 须在 hal_vfd 和 hal_io 注册完成后调用，且须在所有 domain setup 之前调用。
 *
 * @return SW_OK         成功。
 *         SW_ERR_NOT_INIT hal_vfd 或 hal_io 未注册。
 *         SW_ERR_HW    motor_init 校验失败。
 *         SW_ERR_OVERFLOW tick 注册位置已满。
 */
sw_err_t m8_motor_exec_init(void);

/**
 * @brief 返回共享执行器指针，供各 domain setup 调用 domain_init 时注入。
 *
 * @return 已完成 motor_init 的执行器指针；若 m8_motor_exec_init 未调用则为 NULL。
 */
motor_executor_t *m8_motor_exec_get(void);

#ifdef __cplusplus
}
#endif

#endif /* MACHINES_M8_ADAPTERS_SETUP_M8_MOTOR_EXEC_H */
