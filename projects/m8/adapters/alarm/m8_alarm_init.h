/**
 * @file    m8_alarm_init.h
 * @brief   M8 机型报警目录装配（合并三张表，一次注入 alarm_core）
 * @author  HUWANGWEI
 * @date    2026-06-28
 *
 * @note    合并 m8_alarm_table.h（DI 报警）、m8_comm_watchdog_table.h（周期通讯）
 *          和 m8_sw_alarm_table.h（按需通讯 + 软件逻辑）三张配置表，
 *          通过 alarm_binding_port.load_catalog 一次性注入 alarm_core。
 *
 *          调用顺序：alarm_core_init() → m8_alarm_init() → m8_alarm_adapt_init()
 *                                                        → m8_comm_watchdog_init()
 */

#ifndef ADAPTERS_MACHINE_M8_ALARM_INIT_H
#define ADAPTERS_MACHINE_M8_ALARM_INIT_H

#ifdef __cplusplus
extern "C" {
#endif

#include "framework/common/sw_error.h"

/**
 * @brief  合并全部 M8 报警表并注入 alarm_core 目录（一次性操作）
 * @retval SW_OK / SW_ERR_NOT_INIT（alarm_binding_port 未注册）
 *         / SW_ERR_OVERFLOW（条目总数超过 ALARM_CATALOG_MAX）
 * @note   须在 alarm_core_init() 之后、m8_alarm_adapt_init() 之前调用
 */
sw_err_t m8_alarm_init(void);

#ifdef __cplusplus
}
#endif

#endif /* ADAPTERS_MACHINE_M8_ALARM_INIT_H */
