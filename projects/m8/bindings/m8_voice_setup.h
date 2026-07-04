/**
 * @file    m8_voice_setup.h
 * @brief   M8 机型语音模块绑定初始化
 * @author  HUWANGWEI
 * @date    2026-06-29
 */

#ifndef ADAPTERS_MACHINE_M8_VOICE_SETUP_H
#define ADAPTERS_MACHINE_M8_VOICE_SETUP_H

#ifdef __cplusplus
extern "C" {
#endif

#include "framework/common/sw_error.h"

/**
 * @brief  初始化语音模块实例并注册通信事件回调（告警联动）
 * @note   须在 hal_voice_linux_register() 之后调用
 */
sw_err_t m8_voice_setup(void);

#ifdef __cplusplus
}
#endif

#endif /* ADAPTERS_MACHINE_M8_VOICE_SETUP_H */
