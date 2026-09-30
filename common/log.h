/**
 * @file    log.h
 * @brief   统一日志宏（后端可插拔，通过 sw_log_register_sink 注册）
 * @author  HUWANGWEI
 * @date    2026-04-07
 *
 * @note    common 不依赖任何具体 runtime/adapter；未注册 sink 时，
 *          sw_log_write() 使用内置的 stderr 输出兜底。
 */

#ifndef COMMON_LOG_H
#define COMMON_LOG_H

#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <time.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SW_LOG_COMPONENT_BASE "Base"

/** 异步日志队列容量（条）；队列满时新日志丢弃并计入 sw_log_dropped_count() */
#define SW_LOG_SPOOL_CAP 64U

/** 单条物理行容量（字节）。格式化缓冲在 log 模块内，不占用调用方栈 */
#define SW_LOG_LINE_MAX 1024U

/** 行尾截断标记（含前导空格） */
#define SW_LOG_TRUNCATED_MARK " ...(truncated)"

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
 * @brief  按缓冲容量收口一行
 * @param  line     snprintf/vsnprintf 已写入的缓冲
 * @param  cap      缓冲大小（含 NUL）
 * @param  written  格式化返回值，即未截断时应有的正文长度
 * @return 收口后正文长度（不含 NUL）
 * @note   written >= cap 时把行尾换成 SW_LOG_TRUNCATED_MARK。
 *         按拼装后的实测长度判断，不预留前缀余量。
 */
size_t sw_log_clip_line(char *line, size_t cap, int written);

/**
 * @brief  打开异步排出总开关
 * @note   须在 log_drain 线程已由 scheduler_start_all() 启动之后调用。
 *         只影响已调用 sw_log_mark_thread_async() 的线程；其余线程始终同步写出。
 */
void sw_log_enable_async(void);

/**
 * @brief  把调用线程标记为异步写日志线程
 * @note   供 SCHED_FIFO 线程在入口处调用：此后该线程的日志只格式化并入队，
 *         连同调用时的 trace 上下文与发生时刻一起交给 log_drain 线程写 sink，
 *         FIFO 路径上不做 stdio。队列满时丢弃并计数，不回退同步写出。
 *         发生时刻在 sw_log_write() 调用点采样（CLOCK_REALTIME），sink 经
 *         sw_log_occurred_at() 读取，不得再自行取当前时间。
 */
void sw_log_mark_thread_async(void);

/**
 * @brief  读取当前正在写出的日志的发生时刻
 * @param  out 输出 CLOCK_REALTIME；不得为 NULL
 * @retval true  当前处于 sink 回调且调用点采样成功
 * @retval false 不在 sink 回调中、参数无效，或调用点采样失败
 * @note   仅在 sink 回调期间有效，语义与 trace_context_get() 相同：由
 *         sw_log_write() / drain 在调 sink 前写入，返回后失效。
 */
bool sw_log_occurred_at(struct timespec *out);

/**
 * @brief  把墙上时钟格式化为本地时间字符串
 * @param  ts        发生时刻；为 NULL 时使用 sw_log_occurred_at()，读不到则输出 "-"
 * @param  buffer    输出缓冲
 * @param  size      缓冲大小（含 NUL）
 * @param  with_date true 带日期（YYYY-MM-DD HH:MM:SS.mmm）；false 仅 HH:MM:SS.mmm
 * @return 写入的字符数（不含 NUL）；失败时写入 "-" 并返回其长度，参数无效返回 0
 */
size_t sw_log_format_occurred_at(const struct timespec *ts, char *buffer, size_t size, bool with_date);

/**
 * @brief  异步队列满而丢弃的日志条数（上电累计）
 * @return 丢弃条数
 */
uint32_t sw_log_dropped_count(void);

/**
 * @brief  日志排出线程入口，由 bootstrap 登记到线程表
 * @param  arg 未使用
 * @note   无新日志时在条件变量上限时等待，不空转轮询。
 */
void *sw_log_drain_thread_fn(void *arg);

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

#endif /* COMMON_LOG_H */
