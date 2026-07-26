/**
 * @file    log.h
 * @brief   统一日志宏（后端可插拔，通过 sw_log_register_sink 注册）
 * @author  HUWANGWEI
 * @date    2026-04-07
 *
 * @note    common 不依赖任何具体 runtime/adapter；未注册 sink 时，
 *          sw_log_write() 使用内置的 stderr 输出兜底。
 */

#ifndef LOG_H
#define LOG_H

#include <stdarg.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    SW_LOG_ERROR = 0,
    SW_LOG_WARN,
    SW_LOG_INFO,
    SW_LOG_DEBUG,
} sw_log_level_t;

typedef void (*sw_log_sink_fn_t)(sw_log_level_t level, const char *fmt, va_list ap);

/** @brief  注册日志 sink；传 NULL 恢复内置 stderr 输出 */
void sw_log_register_sink(sw_log_sink_fn_t sink);

/** @brief  写一条日志，转发给已注册的 sink 或内置 stderr 输出 */
void sw_log_write(sw_log_level_t level, const char *fmt, ...);

/**
 * @brief 从源码路径中提取文件名
 *
 * @param source_path 编译器提供的源码路径，可为 NULL
 * @return 文件名；输入为 NULL 时返回空字符串
 */
const char *sw_log_source_file_name(const char *source_path);

#ifdef __cplusplus
}
#endif

/* -------------------------------------------------------------------------
 * 日志宏 — 自动附加 [文件:行号] 前缀
 * 使用示例：
 *   LOG_INFO("motor speed set to %d rpm", speed);
 * ------------------------------------------------------------------------- */
#define LOG_ERROR(fmt, ...) \
    sw_log_write(SW_LOG_ERROR, "[%s:%d] " fmt, sw_log_source_file_name(__FILE__), __LINE__, ##__VA_ARGS__)

#define LOG_WARN(fmt, ...) \
    sw_log_write(SW_LOG_WARN, "[%s:%d] " fmt, sw_log_source_file_name(__FILE__), __LINE__, ##__VA_ARGS__)

#define LOG_INFO(fmt, ...) \
    sw_log_write(SW_LOG_INFO, "[%s:%d] " fmt, sw_log_source_file_name(__FILE__), __LINE__, ##__VA_ARGS__)

#define LOG_DEBUG(fmt, ...) \
    sw_log_write(SW_LOG_DEBUG, "[%s:%d] " fmt, sw_log_source_file_name(__FILE__), __LINE__, ##__VA_ARGS__)

#endif /* LOG_H */
