/**
 * @file    snack_log.h
 * @brief   snack SDK 日志接口
 */

#ifndef SNACK_LOG_H
#define SNACK_LOG_H

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 输出 ERROR 级别日志
 * @param fmt printf 格式字符串
 */
extern void snack_log_error(const char *fmt, ...);

/**
 * @brief 输出 WARN 级别日志
 * @param fmt printf 格式字符串
 */
extern void snack_log_warn(const char *fmt, ...);

/**
 * @brief 输出 INFO 级别日志
 * @param fmt printf 格式字符串
 */
extern void snack_log_info(const char *fmt, ...);

/**
 * @brief 输出 DEBUG 级别日志
 * @param fmt printf 格式字符串
 */
extern void snack_log_debug(const char *fmt, ...);

/**
 * @brief 设置日志输出级别
 * @param type 级别值（对应 snack SDK MLOG_type 枚举）
 */
extern void set_log_level(int type);

#ifdef __cplusplus
}
#endif

#endif /* SNACK_LOG_H */
