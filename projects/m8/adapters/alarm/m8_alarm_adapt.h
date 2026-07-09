/**
 * @file    m8_alarm_adapt.h
 * @brief   M8 机型报警绑定适配（DI 防抖轮询 + comm watchdog + event drain）
 * @author  HUWANGWEI
 * @date    2026-07-09
 *
 * @note    init 时预热 DI 防抖状态；poll 线程周期执行
 *          m8_alarm_adapt_poll → m8_comm_watchdog_poll → alarm_event_bridge_drain。
 */

#ifndef ADAPTERS_MACHINE_M8_ALARM_ADAPT_H
#define ADAPTERS_MACHINE_M8_ALARM_ADAPT_H

#ifdef __cplusplus
extern "C" {
#endif

#include "framework/common/sw_error.h"

/**
 * @brief  DI 防抖预热（目录由 m8_alarm_init 注入）
 * @retval SW_OK
 * @note   须在 m8_alarm_init() 之后调用
 */
sw_err_t m8_alarm_adapt_init(void);

/**
 * @brief  执行一次 DI 边沿检测
 */
void m8_alarm_adapt_poll(void);

/**
 * @brief  启动 io_poll 线程（~30ms）
 * @retval SW_OK / SW_ERR_HW
 * @note   须在 m8_alarm_adapt_init() 之后调用
 */
sw_err_t m8_alarm_adapt_poll_start(void);

#ifdef __cplusplus
}
#endif

#endif /* ADAPTERS_MACHINE_M8_ALARM_ADAPT_H */
