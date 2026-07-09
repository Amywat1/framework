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

#include "framework/common/sw_error.h"

sw_err_t alarm_event_bridge_init(void);
void     alarm_event_bridge_drain(void);

#ifdef __cplusplus
}
#endif

#endif /* APPLICATION_ALARM_EVENT_BRIDGE_H */
