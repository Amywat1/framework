/**
 * @file    m8_signal_filter.h
 * @brief   M8 表驱动信号滤波接口
 * @author  胡望伟
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
 * @brief  初始化信号滤波运行时状态
 * @note   由 bootstrap 在 alarm_core_init() 之后调用。
 */
void m8_signal_filter_init(void);

/**
 * @brief  执行一次信号滤波时间片
 * @note   周期由外部调用方保证，当前设计为 10ms 一次。
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
