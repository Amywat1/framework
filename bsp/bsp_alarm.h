/**
 * @file    bsp_alarm.h
 * @brief   M8 机型报警适配层接口（将机型特定逻辑注入通用报警引擎）
 * @author  HUWANGWEI
 * @date    2026-04-07
 */

#ifndef BSP_ALARM_H
#define BSP_ALARM_H

#include "common/sw_error.h"

/**
 * @brief  初始化并注册所有 M8 报警回调到 svc_alarm
 * @retval SW_OK
 * @note   必须在 hal_init() 和 svc_alarm_init() 之后调用
 */
sw_err_t bsp_alarm_init(void);

#endif /* BSP_ALARM_H */
