/**
 * @file    hal_io_sim.h
 * @brief   仿真 DI 注入接口（供 hal_sensor_sim 与测试使用）
 */

#ifndef ADAPTERS_HAL_SIM_HW_HAL_IO_SIM_H
#define ADAPTERS_HAL_SIM_HW_HAL_IO_SIM_H

#ifdef __cplusplus
extern "C" {
#endif

#include "common/io_handle.h"
#include <stdbool.h>

/**
 * @brief  设置仿真 DI 原始电平（极性转换由 signal_filter 处理）
 * @param  pin    DI 句柄
 * @param  level  true=高电平，false=低电平
 */
void hal_io_sim_set_di_level(io_di_t pin, bool level);

#ifdef __cplusplus
}
#endif

#endif /* ADAPTERS_HAL_SIM_HW_HAL_IO_SIM_H */
