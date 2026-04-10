/**
 * @file    m8_boot_profile.h
 * @brief   M8 上电安全初始化接口
 * @author  胡望伟
 * @date    2026-04-10
 */

#ifndef ADAPTERS_MACHINE_M8_BOOT_PROFILE_H
#define ADAPTERS_MACHINE_M8_BOOT_PROFILE_H

#ifdef __cplusplus
extern "C" {
#endif

#include "common/sw_error.h"

/**
 * @brief  M8 上电启动安全初始化
 *         轮询等待 IO 子板就绪，然后将所有 DO 置安全状态。
 *         始终返回 SW_OK（超时时打告警但继续，见函数注释）。
 */
sw_err_t m8_boot_profile_init(void);

#ifdef __cplusplus
}
#endif

#endif /* ADAPTERS_MACHINE_M8_BOOT_PROFILE_H */
