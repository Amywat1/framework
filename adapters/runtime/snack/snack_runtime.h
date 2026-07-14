/**
 * @file    snack_runtime.h
 * @brief   Snack 进程运行时细粒度接口
 */

#ifndef SNACK_RUNTIME_H
#define SNACK_RUNTIME_H

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 设置 Snack 运行时日志级别
 * @param type 级别值
 */
void snack_runtime_set_log_level(int type);

/**
 * @brief 设置 Snack 运行时远程调试端口
 * @param port 端口号
 */
void snack_runtime_set_remote_port(int port);

#ifdef __cplusplus
}
#endif

#endif /* SNACK_RUNTIME_H */
