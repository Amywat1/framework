/**
 * @file    m8_signal_filter.h
 * @brief   M8 滤波信号查询接口
 * @author  HUWANGWEI
 * @date    2026-04-13
 */

#ifndef ADAPTERS_MACHINE_M8_SIGNAL_FILTER_H
#define ADAPTERS_MACHINE_M8_SIGNAL_FILTER_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>

#include "config/machine/m8_signal_table.h"

/**
 * @brief  执行一次 hal_sensor 滤波时间片
 * @note   周期由外部调用方保证，当前设计为 ALARM_POLL_PERIOD_MS 一次。
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

#endif /* ADAPTERS_MACHINE_M8_SIGNAL_FILTER_H */
