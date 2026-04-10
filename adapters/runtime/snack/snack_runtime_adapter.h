/**
 * @file    snack_runtime_adapter.h
 * @brief   snack 运行时适配接口
 * @author  胡望伟
 * @date    2026-04-10
 *
 * @note    负责接入 snack 运行环境相关的外围能力，
 *          包括 IO 子板日志桥接和调试远程端口设置。
 */

#ifndef ADAPTERS_RUNTIME_SNACK_RUNTIME_ADAPTER_H
#define ADAPTERS_RUNTIME_SNACK_RUNTIME_ADAPTER_H

/**
 * @brief  初始化 snack 运行时适配
 */
void snack_runtime_adapter_init(void);

#endif /* ADAPTERS_RUNTIME_SNACK_RUNTIME_ADAPTER_H */
