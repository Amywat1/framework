/**
 * @file    alarm_lifecycle_bridge.h
 * @brief   报警注册表洗车会话生命周期桥接
 * @author  HUWANGWEI
 * @date    2026-07-14
 */

#ifndef APPLICATION_BRIDGES_ALARM_LIFECYCLE_BRIDGE_H
#define APPLICATION_BRIDGES_ALARM_LIFECYCLE_BRIDGE_H

#ifdef __cplusplus
extern "C" {
#endif

#include "common/sw_error.h"

/**
 * @brief  订阅洗车会话事件，同步通知 alarm_registry 会话开始/结束
 * @retval SW_OK 订阅成功
 */
sw_err_t alarm_lifecycle_bridge_init(void);

#ifdef __cplusplus
}
#endif

#endif /* APPLICATION_BRIDGES_ALARM_LIFECYCLE_BRIDGE_H */
