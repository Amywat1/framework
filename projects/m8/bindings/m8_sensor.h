/**
 * @file    m8_sensor.h
 * @brief   M8 传感器子系统（DI 绑定、预热、信号查询）
 * @author  HUWANGWEI
 * @date    2026-06-21
 */

#ifndef ADAPTERS_MACHINE_M8_SENSOR_H
#define ADAPTERS_MACHINE_M8_SENSOR_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>

#include "framework/common/sw_error.h"
#include "projects/m8/config/m8_signal_table.h"

/**
 * @brief  按 m8_signal_table 绑定 hal_sensor 通道并初始化滤波运行时
 * @note   须在 hal_sensor 已 register 且 hal_io.init 之后调用
 */
sw_err_t m8_sensor_setup(void);

/**
 * @brief  同步执行 max(trig_count) 次传感器预热采样，预填充滤波状态
 * @note   须在 m8_sensor_setup() 与仿真 m8_signal_sim_reset_all() 之后、
 *         hal_sensor 周期任务启动前调用，以消除上电时限位信号确认延迟
 */
sw_err_t m8_sensor_warmup(void);

/**
 * @brief  查询信号滤波后的稳定状态
 * @param  sig_id  信号标识
 * @retval true=触发态，false=释放态或参数无效
 */
bool m8_signal_is_active(m8_signal_id_t sig_id);

#ifdef __cplusplus
}
#endif

#endif /* ADAPTERS_MACHINE_M8_SENSOR_H */
