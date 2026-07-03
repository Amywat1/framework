/**
 * @file    m8_motor_exec.h
 * @brief   M8 机型共享电机执行器接口。
 *
 * 全部受 MCC 管理的电机（龙门、侧刷、顶刷、升降、后轮锁止、风机）共享一个
 * motor_executor_t，由 m8_motor_exec_init() 统一完成 motor_init，
 * m8_motor_exec_start() 启动后台线程按 20ms 节拍调用 motor_tick 推进全部 6 个轴。
 *
 * 电机索引：
 *   M8_MOTOR_GANTRY     = 0   龙门行走
 *   M8_MOTOR_BRUSH_SIDE = 1   侧刷（与顶刷共用 VFD，靠接触器切换）
 *   M8_MOTOR_BRUSH_TOP  = 2   顶刷（与侧刷共用 VFD，靠接触器切换）
 *   M8_MOTOR_LIFT       = 3   顶刷升降
 *   M8_MOTOR_REAR_LOCK  = 4   后轮锁止推杆
 *   M8_MOTOR_FAN        = 5   风机 VFD
 *
 * 侧刷/顶刷通过各自的 motor_driver_t::prepare 回调驱动接触器切换时序
 * （m8_motor_exec.c 内部实现），并以 MOTOR_INTERLOCK_MUTEX 双向互锁保证
 * 二者不会同时处于运行相关态。
 *
 * 调用顺序：
 *   m8_motor_exec_init()  → 建立执行器
 *   m8_gantry_setup()     → gantry_init(exec, M8_MOTOR_GANTRY)
 *   m8_brush_setup()      → brush_init(exec, M8_MOTOR_BRUSH_SIDE, M8_MOTOR_BRUSH_TOP)
 *   m8_lift_setup()       → lift_init(exec, M8_MOTOR_LIFT)
 *   m8_rear_lock_setup()  → rear_lock_init(exec, M8_MOTOR_REAR_LOCK)
 *   m8_fan_setup()        → fan_init(exec, M8_MOTOR_FAN)
 *   m8_motor_exec_start() → 启动 tick 线程
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
/** 侧刷电机索引（与顶刷共用 VFD，靠接触器切换） */
#define M8_MOTOR_BRUSH_SIDE 1
/** 顶刷电机索引（与侧刷共用 VFD，靠接触器切换） */
#define M8_MOTOR_BRUSH_TOP  2
/** 顶刷升降继电器电机索引 */
#define M8_MOTOR_LIFT       3
/** 后轮锁止继电器电机索引 */
#define M8_MOTOR_REAR_LOCK  4
/** 风机 VFD 电机索引 */
#define M8_MOTOR_FAN        5

/**
 * @brief 初始化共享 motor_executor_t，配置全部 6 路电机。
 *
 * 须在 hal_vfd 和 hal_io 注册完成后调用，且须在所有 domain setup 之前调用。
 *
 * @return SW_OK         成功。
 *         SW_ERR_NOT_INIT hal_vfd 或 hal_io 未注册。
 *         SW_ERR_HW    motor_init 校验失败。
 */
sw_err_t m8_motor_exec_init(void);

/**
 * @brief 返回共享执行器指针，供各 domain setup 调用 domain_init 时注入。
 *
 * @return 已完成 motor_init 的执行器指针；若 m8_motor_exec_init 未调用则为 NULL。
 */
motor_executor_t *m8_motor_exec_get(void);

/**
 * @brief 启动共享执行器的 tick 后台线程（分离式，20ms 节拍）。
 *
 * 须在所有电机 setup 完成后、scheduler_start_all() 之前调用。
 *
 * @return SW_OK 成功；SW_ERR_HW 线程创建失败。
 */
sw_err_t m8_motor_exec_start(void);

#ifdef __cplusplus
}
#endif

#endif /* MACHINES_M8_ADAPTERS_SETUP_M8_MOTOR_EXEC_H */
