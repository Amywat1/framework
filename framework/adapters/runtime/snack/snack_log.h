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
 * @brief 将 framework/common/log.h 的日志 sink 注册为 snack SDK 实现
 * @note  供项目 wiring 在真机构建路径下调用一次。
 */
extern void snack_log_sink_register(void);

/**
 * @brief 设置日志输出级别
 * @param type 级别值（对应 snack SDK MLOG_type 枚举）
 */
extern void set_log_level(int type);

#ifdef __cplusplus
}
#endif

#endif /* SNACK_LOG_H */
