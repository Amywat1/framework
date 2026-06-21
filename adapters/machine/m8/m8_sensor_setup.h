/**
 * @file    m8_sensor_setup.h
 * @brief   M8 机型 DI 通道绑定
 * @author  HUWANGWEI
 * @date    2026-06-07
 */

#ifndef ADAPTERS_MACHINE_M8_SENSOR_SETUP_H
#define ADAPTERS_MACHINE_M8_SENSOR_SETUP_H

#ifdef __cplusplus
extern "C" {
#endif

#include "common/sw_error.h"

/**
 * @brief  按 m8_signal_table 绑定 hal_sensor 通道并初始化滤波运行时
 * @note   须在 hal_sensor 已 register 且 hal_io.init 之后调用
 */
sw_err_t m8_sensor_setup(void);

/**
 * @brief  同步执行 max(trig_count) 次 sensor->tick()，将 s_rt[].confirmed
 *         预填充为当前物理/仿真 DI 状态
 * @note   须在 m8_sensor_setup() 与仿真 m8_signal_sim_reset_all() 之后、
 *         io_poll 线程启动前调用，以消除上电时限位信号确认延迟
 */
sw_err_t m8_sensor_warmup(void);

#ifdef __cplusplus
}
#endif

#endif /* ADAPTERS_MACHINE_M8_SENSOR_SETUP_H */
