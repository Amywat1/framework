/**
 * @file    hal_io_sim.h
 * @brief   仿真 DI 注入接口（供项目信号仿真层与测试使用）
 */

#ifndef ADAPTERS_OUTBOUND_HAL_SIM_HAL_IO_SIM_H
#define ADAPTERS_OUTBOUND_HAL_SIM_HAL_IO_SIM_H

#ifdef __cplusplus
extern "C" {
#endif

#include "common/io_handle.h"
#include "common/sw_error.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/** @brief 仿真 IO 名称表条目（与机型 DO_/DI_ 规范名表布局一致）。 */
typedef struct {
    const char *name; /**< 标准名称，如 "DO_GANTRY_FWD" */
    uint16_t    raw;  /**< 句柄底层编码 */
} hal_io_sim_name_entry_t;

/** @brief  注册 hal_io_sim 操作集到 hal_io_port；不隐式初始化仿真状态。 */
void hal_io_sim_register(void);

/**
 * @brief  注册仿真 DO 名称表，供 do_name / try_parse_do 与变更日志使用。
 * @param  table  名称表；可为 NULL 表示清除。
 * @param  count  条目数；table 为 NULL 时忽略。
 * @note   表须在进程生命周期内保持有效（通常为静态表）。
 */
void hal_io_sim_set_do_names(const hal_io_sim_name_entry_t *table, size_t count);

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
 * @brief 读取仿真数字输出电平。
 * @param pin   数字输出句柄。
 * @param level 输出电平接收地址。
 * @retval SW_OK 成功。
 * @retval SW_ERR_PARAM 参数非法。
 * @retval SW_ERR_NOT_INIT 仿真 IO 尚未初始化。
 */
sw_err_t hal_io_sim_get_do_level(io_do_t pin, bool *level);

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

#endif /* ADAPTERS_OUTBOUND_HAL_SIM_HAL_IO_SIM_H */
