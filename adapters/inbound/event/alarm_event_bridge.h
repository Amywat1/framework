/**
 * @file    alarm_event_bridge.h
 * @brief   报警域事件桥接（Registry pending → event_bus）
 * @author  HUWANGWEI
 * @date    2026-07-09
 */

#ifndef APPLICATION_ALARM_EVENT_BRIDGE_H
#define APPLICATION_ALARM_EVENT_BRIDGE_H

#ifdef __cplusplus
extern "C" {
#endif

#include "common/sw_error.h"

/**
 * @brief  初始化报警事件桥接并注册 50ms 周期 drain 任务
 * @retval SW_OK         注册成功
 * @retval SW_ERR_OVERFLOW 周期任务表已满
 */
sw_err_t alarm_event_bridge_init(void);

#ifdef __cplusplus
}
#endif

#endif /* APPLICATION_ALARM_EVENT_BRIDGE_H */
