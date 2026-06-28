/**
 * @file    m8_boot_profile.h
 * @brief   M8 上电安全初始化接口
 * @author  HUWANGWEI
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

/**
 * @brief  立即将所有数字输出置安全状态（关断）并同步刷新到硬件
 * @note   更新输出缓冲后立即经 hal_io flush 同步到硬件，
 *         绕过后台线程，直接写 CAN 总线（尽力而为，离线子板跳过）。
 *         可在 panic 路径（abort() 前）安全调用，不依赖 event_bus。
 */
void m8_assert_safe_outputs(void);

#ifdef __cplusplus
}
#endif

#endif /* ADAPTERS_MACHINE_M8_BOOT_PROFILE_H */
