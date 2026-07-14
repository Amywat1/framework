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
 * @brief 将 common/log.h 的日志 sink 注册为 snack SDK 实现
 * @param name 日志实例名；传 NULL 或空串时使用默认名
 * @note  供项目 wiring 在真机构建路径下调用一次。
 */
extern void snack_log_sink_register(const char *name);

#ifdef __cplusplus
}
#endif

#endif /* SNACK_LOG_H */
