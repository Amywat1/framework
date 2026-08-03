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
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SW_LOG_COMPONENT_BASE "Base"

#ifndef SW_LOG_COMPONENT
#define SW_LOG_COMPONENT SW_LOG_COMPONENT_BASE
#endif

typedef enum {
    SW_LOG_ERROR = 0,
    SW_LOG_WARN,
    SW_LOG_INFO,
    SW_LOG_DEBUG,
} sw_log_level_t;

typedef void (*sw_log_sink_fn_t)(sw_log_level_t level, const char *component, const char *fmt, va_list ap);

/**
 * @brief  设置运行期日志级别门限
 * @param  level 允许输出的最低级别（数值越大越详细）；
 *               例如设为 SW_LOG_INFO 时，SW_LOG_DEBUG 被丢弃
 * @note   上电默认为 SW_LOG_DEBUG（全部输出）。级别过滤在格式化之前完成，
 *         被丢弃的日志不产生 vsnprintf 开销。
 */
void sw_log_set_level(sw_log_level_t level);

/**
 * @brief  读取当前日志级别门限
 */
sw_log_level_t sw_log_get_level(void);

/**
 * @brief  判断给定级别当前是否会被输出
 * @param  level 待判断级别
 * @retval true  该级别会被输出
 * @note   供调用方在构造昂贵日志参数前提前短路使用
 */
bool sw_log_level_enabled(sw_log_level_t level);

/** @brief  注册日志 sink；传 NULL 恢复内置 stderr 输出 */
void sw_log_register_sink(sw_log_sink_fn_t sink);

/**
 * @brief 追加一个日志 sink
 * @param sink 待追加的 sink，不能为空
 * @retval true 追加成功或 sink 已存在
 * @retval false 参数无效或 sink 数量已满
 */
bool sw_log_add_sink(sw_log_sink_fn_t sink);

/** @brief  写一条日志，转发给已注册的 sink 或内置 stderr 输出 */
void sw_log_write(sw_log_level_t level, const char *component, const char *fmt, ...);

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
#define LOG_ERROR(fmt, ...)                                                                                            \
    sw_log_write(                                                                                                      \
        SW_LOG_ERROR, SW_LOG_COMPONENT, "[%s:%d] " fmt, sw_log_source_file_name(__FILE__), __LINE__, ##__VA_ARGS__)

#define LOG_WARN(fmt, ...)                                                                                             \
    sw_log_write(                                                                                                      \
        SW_LOG_WARN, SW_LOG_COMPONENT, "[%s:%d] " fmt, sw_log_source_file_name(__FILE__), __LINE__, ##__VA_ARGS__)

#define LOG_INFO(fmt, ...)                                                                                             \
    sw_log_write(                                                                                                      \
        SW_LOG_INFO, SW_LOG_COMPONENT, "[%s:%d] " fmt, sw_log_source_file_name(__FILE__), __LINE__, ##__VA_ARGS__)

#define LOG_DEBUG(fmt, ...)                                                                                            \
    sw_log_write(                                                                                                      \
        SW_LOG_DEBUG, SW_LOG_COMPONENT, "[%s:%d] " fmt, sw_log_source_file_name(__FILE__), __LINE__, ##__VA_ARGS__)

#endif /* LOG_H */
