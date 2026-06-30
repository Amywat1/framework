/**
 * @file    m8_runtime_adapter.h
 * @brief   M8 机型运行时适配接口
 * @author  HUWANGWEI
 * @date    2026-06-23
 *
 * @note    M8 专属的 snack SDK 运行时配置（远程调试端口、IO 子板日志回调）。
 *          不可跨机型复用；通用 SDK 封装见 middleware/snack/snack_wrapper.h。
 */

#ifndef ADAPTERS_MACHINE_M8_RUNTIME_ADAPTER_H
#define ADAPTERS_MACHINE_M8_RUNTIME_ADAPTER_H

#ifdef __cplusplus
extern "C" {
#endif

/** @brief 初始化 M8 运行时适配（设置远程调试端口、注册 IO 子板日志回调） */
void m8_runtime_adapter_init(void);

#ifdef __cplusplus
}
#endif

#endif /* ADAPTERS_MACHINE_M8_RUNTIME_ADAPTER_H */
