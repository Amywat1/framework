/**
 * @file    m8_motor_tick.h
 * @brief   M8 电机 tick 统一调度管理器。
 *
 * 通用周期 tick 调度管理器。
 * 所有需要以固定节拍调度的函数均通过 m8_motor_tick_register() 注册，
 * bootstrap 最后统一调用 m8_motor_tick_start() 启动单一后台线程。
 *
 * 调用顺序：
 *   1. m8_motor_exec_init() → m8_motor_tick_register(m8_motor_exec_tick)
 *   2. m8_brush_setup()     → m8_motor_tick_register(brush_tick)
 *   3. m8_fan_setup()       → m8_motor_tick_register(fan_tick)
 *   4. m8_motor_tick_start()
 */
#ifndef MACHINES_M8_ADAPTERS_SETUP_M8_MOTOR_TICK_H
#define MACHINES_M8_ADAPTERS_SETUP_M8_MOTOR_TICK_H

#include "common/sw_error.h"

#ifdef __cplusplus
extern "C" {
#endif

/** 最多可注册的 tick 函数数量（motor_exec_tick + brush_tick + fan_tick + 备用）*/
#define M8_MOTOR_TICK_MAX  8

/**
 * @brief 注册一个电机 tick 函数，在 tick 线程中按注册顺序依次调用。
 *
 * @param tick_fn 周期 tick 函数，不得为 NULL。
 * @return SW_OK 成功；SW_ERR_PARAM 参数为 NULL；SW_ERR_OVERFLOW 超出上限。
 *
 * @note 须在 m8_motor_tick_start() 之前调用，通常在各电机 setup 函数末尾注册。
 */
sw_err_t m8_motor_tick_register(void (*tick_fn)(void));

/**
 * @brief 启动电机 tick 后台线程（分离式，20ms 节拍）。
 *
 * 线程依次调用所有已注册的 tick 函数。须在所有电机 setup 完成后、
 * scheduler_start_all() 之前调用。
 *
 * @return SW_OK 成功；SW_ERR_HW 线程创建失败。
 */
sw_err_t m8_motor_tick_start(void);

#ifdef __cplusplus
}
#endif

#endif /* MACHINES_M8_ADAPTERS_SETUP_M8_MOTOR_TICK_H */
