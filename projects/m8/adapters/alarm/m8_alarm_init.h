/**
 * @file    m8_alarm_init.h
 * @brief   M8 机型报警目录装配（合并三张表，一次注入 alarm_registry）
 * @author  HUWANGWEI
 * @date    2026-07-09
 *
 * @note    合并 m8_alarm_table.h（DI 报警）、m8_alarm_comm_table.h（周期通讯）
 *          与 m8_alarm_sw_table.h（流程/调用点），
 *          通过 alarm_binding_port.load_catalog 一次性注入 alarm_registry。
 *
 *          调用顺序：alarm_registry_init() → m8_alarm_init() → m8_alarm_adapt_init()
 */

#ifndef ADAPTERS_MACHINE_M8_ALARM_INIT_H
#define ADAPTERS_MACHINE_M8_ALARM_INIT_H

#ifdef __cplusplus
extern "C" {
#endif

#include "framework/common/sw_error.h"

/**
 * @brief  合并全部 M8 报警表并注入 alarm_registry 目录（一次性操作）
 * @retval SW_OK / SW_ERR_NOT_INIT / SW_ERR_OVERFLOW
 * @note   须在 alarm_registry_init() 之后、m8_alarm_adapt_init() 之前调用
 */
sw_err_t m8_alarm_init(void);

#ifdef __cplusplus
}
#endif

#endif /* ADAPTERS_MACHINE_M8_ALARM_INIT_H */
