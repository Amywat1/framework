/**
 * @file    m8_sensor.h
 * @brief   M8 传感器子系统（DI 绑定、预热、轮询线程、信号查询）
 * @author  HUWANGWEI
 * @date    2026-06-21
 */

#ifndef ADAPTERS_MACHINE_M8_SENSOR_H
#define ADAPTERS_MACHINE_M8_SENSOR_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>

#include "common/sw_error.h"
#include "machines/m8/config/m8_signal_table.h"

/**
 * @brief  按 m8_signal_table 绑定 hal_sensor 通道并初始化滤波运行时
 * @note   须在 hal_sensor 已 register 且 hal_io.init 之后调用
 */
sw_err_t m8_sensor_setup(void);

/**
 * @brief  同步执行 max(trig_count) 次 sensor->tick()，预填充滤波状态
 * @note   须在 m8_sensor_setup() 与仿真 m8_signal_sim_reset_all() 之后、
 *         轮询线程启动前调用，以消除上电时限位信号确认延迟
 */
sw_err_t m8_sensor_warmup(void);

/**
 * @brief  启动 IO 轮询线程（周期调用 m8_signal_filter_tick 和 m8_alarm_adapt_poll）
 * @note   须在 m8_sensor_setup() 之后调用；线程自管，不经 scheduler
 */
sw_err_t m8_sensor_poll_start(void);

/**
 * @brief  执行一次 hal_sensor 滤波时间片
 * @note   由轮询线程周期驱动；场景测试可直接调用以替代线程
 */
void m8_signal_filter_tick(void);

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
