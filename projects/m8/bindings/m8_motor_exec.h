/**
 * @file    m8_motor_exec.h
 * @brief   M8 机型共享电机执行器接口。
 *
 * 全部受 MCC 管理的电机（龙门、侧刷、顶刷、升降、后轮锁止、风机）共享一个
 * motor_executor_t，由 m8_motor_exec_init() 统一完成 motor_init，
 * m8_motor_exec_start() 启动后台线程按 20ms 节拍调用 motor_tick 推进全部 6 个轴。
 *
 * 电机索引见 m8_motor_id_t；增删电机时仅需在 M8_MOTOR_COUNT 哨兵前插入一项，
 * 电机总数由枚举自动推导，无需手动维护数量宏。
 *
 * 侧刷/顶刷通过各自的 motor_driver_t::prepare 回调驱动接触器切换时序
 * （m8_motor_exec.c 内部实现），并以 MOTOR_INTERLOCK_MUTEX 双向互锁保证
 * 二者不会同时处于运行相关态。
 *
 * 调用顺序（生产环境推荐）：
 *   m8_machine_setup()        → exec + 领域绑定 + 水路
 *   m8_motor_exec_start()     → 启动 tick 线程
 *
 * 测试或裁剪 init 时可单独调用：
 *   m8_motor_exec_init()
 *   m8_motor_domains_setup_mask(M8_DOMAIN_GANTRY | M8_DOMAIN_BRUSH)
 */
#ifndef MACHINES_M8_ADAPTERS_SETUP_M8_MOTOR_EXEC_H
#define MACHINES_M8_ADAPTERS_SETUP_M8_MOTOR_EXEC_H

#include "motor/motor_executor.h"
#include "framework/common/sw_error.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief M8 机型电机索引（M8_MOTOR_COUNT 为总数哨兵，非有效电机编号）。 */
typedef enum {
    M8_MOTOR_GANTRY = 0, /**< 龙门行走 */
    M8_MOTOR_BRUSH_SIDE, /**< 侧刷（与顶刷共用 VFD，靠接触器切换） */
    M8_MOTOR_BRUSH_TOP,  /**< 顶刷（与侧刷共用 VFD，靠接触器切换） */
    M8_MOTOR_LIFT,       /**< 顶刷升降 */
    M8_MOTOR_REAR_LOCK,  /**< 后轮锁止推杆 */
    M8_MOTOR_FAN,        /**< 风机 VFD */
    M8_MOTOR_COUNT       /**< 电机总数 */
} m8_motor_id_t;

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
