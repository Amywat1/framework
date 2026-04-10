/**
 * @file    m8_alarm_adapt.h
 * @brief   M8 报警适配初始化接口
 * @author  胡望伟
 * @date    2026-04-10
 */

#ifndef ADAPTERS_MACHINE_M8_ALARM_ADAPT_H
#define ADAPTERS_MACHINE_M8_ALARM_ADAPT_H

#ifdef __cplusplus
extern "C" {
#endif

#include "common/sw_error.h"

/**
 * @brief  初始化 M8 报警适配（注册 IO 轮询和急停复位回调到 alarm_core）
 * @note   须在 alarm_core_init() 之后调用
 */
sw_err_t m8_alarm_adapt_init(void);

#ifdef __cplusplus
}
#endif

#endif /* ADAPTERS_MACHINE_M8_ALARM_ADAPT_H */
