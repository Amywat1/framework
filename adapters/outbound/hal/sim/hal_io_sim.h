/**
 * @file    hal_io_sim.h
 * @brief   仿真 DI 注入接口（供 m8_signal_sim 与测试使用）
 */

#ifndef ADAPTERS_HAL_SIM_HW_HAL_IO_SIM_H
#define ADAPTERS_HAL_SIM_HW_HAL_IO_SIM_H

#ifdef __cplusplus
extern "C" {
#endif

#include "common/io_handle.h"
#include "common/sw_error.h"

#include <stdbool.h>
#include <stdint.h>

/** @brief  注册 hal_io_sim 操作集到 hal_io_port；不隐式初始化仿真状态。 */
void hal_io_sim_register(void);

/**
 * @brief  校验本次仿真生命周期中是否发生过初始化前 HAL 访问。
 * @retval SW_OK        未发生生命周期违规。
 * @retval SW_ERR_STATE 发生过至少一次初始化前 HAL 访问。
 */
sw_err_t hal_io_sim_validate_lifecycle(void);

/**
 * @brief  设置仿真 DI 原始电平（极性转换由 signal_filter 处理）
 * @param  pin    DI 句柄
 * @param  level  true=高电平，false=低电平
 */
void hal_io_sim_set_di_level(io_di_t pin, bool level);

/**
 * @brief 设置仿真子板在线状态。
 * @param board_id 子板号，从 1 开始。
 * @param online   true=恢复并产生有效快照，false=输入质量变为 OFFLINE。
 */
void hal_io_sim_set_board_online(int board_id, bool online);

/**
 * @brief  设置仿真脉冲计数器值（供编码器仿真与测试使用）
 * @param  pin    DI 句柄（须为有效仿真引脚）
 * @param  value  计数值
 */
void hal_io_sim_set_pulse_counter(io_di_t pin, uint32_t value);

/**
 * @brief  设置仿真 ADC 读数（raw / mV / mA）
 * @param  board_id  子板号，从 1 开始
 * @param  port      ADC 通道号，范围 1~4
 * @param  raw       原始值
 * @param  mv        电压值（mV）
 * @param  ma        电流值（mA）
 */
void hal_io_sim_set_adc(int board_id, int port, int raw, int mv, int ma);

#ifdef HAL_IO_SIM_UNIT_TEST
void hal_io_sim_test_reset(void);
#endif

#ifdef __cplusplus
}
#endif

#endif /* ADAPTERS_HAL_SIM_HW_HAL_IO_SIM_H */
