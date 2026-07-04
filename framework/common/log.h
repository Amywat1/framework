/**
 * @file    log.h
 * @brief   统一日志宏（封装 snack_log_*，附文件名和行号）
 * @author  HUWANGWEI
 * @date    2026-04-07
 */

#ifndef LOG_H
#define LOG_H

#include "projects/m8/adapters/runtime/snack/snack_log.h"

/* -------------------------------------------------------------------------
 * 日志宏 — 自动附加 [文件:行号] 前缀
 * 使用示例：
 *   LOG_INFO("motor speed set to %d rpm", speed);
 * ------------------------------------------------------------------------- */
#define LOG_ERROR(fmt, ...) \
    snack_log_error("[%s:%d] " fmt, __FILE__, __LINE__, ##__VA_ARGS__)

#define LOG_WARN(fmt, ...) \
    snack_log_warn("[%s:%d] " fmt, __FILE__, __LINE__, ##__VA_ARGS__)

#define LOG_INFO(fmt, ...) \
    snack_log_info("[%s:%d] " fmt, __FILE__, __LINE__, ##__VA_ARGS__)

#define LOG_DEBUG(fmt, ...) \
    snack_log_debug("[%s:%d] " fmt, __FILE__, __LINE__, ##__VA_ARGS__)

#endif /* LOG_H */
