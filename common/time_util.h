/**
 * @file    time_util.h
 * @brief   单调时钟工具（毫秒级时间戳）
 * @author  HUWANGWEI
 * @date    2026-04-10
 *
 * @note    基于 CLOCK_MONOTONIC，不受系统时间调整影响。
 *          time_util_init() 须在 event_bus_init() 之前调用（见 bootstrap 顺序）。
 */

#ifndef COMMON_TIME_UTIL_H
#define COMMON_TIME_UTIL_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <time.h>

/**
 * @brief  初始化时钟工具并读取 Host 时间倍率
 * @note   仅当编译定义 `WDF_ALLOW_TIME_SCALE` 时才接受环境变量
 *         `WDF_TIME_SCALE`（1 / 2 / 5，须整除所有被缩短的周期：
 *         控制环 10 ms、报警轮询 30 ms）。非法值回落到 1 并打日志。
 *         真机目标不得定义该宏，倍率恒为 1。
 */
void time_util_init(void);

/**
 * @brief  当前时间倍率（1 表示墙钟）
 * @retval 1、2 或 5
 */
uint32_t time_util_scale(void);

/**
 * @brief  把虚拟毫秒换成墙钟毫秒，供 nanosleep / usleep 使用
 * @param  virtual_ms  按 `time_util_get_ms()` 计量的时长
 * @retval 墙钟毫秒；`virtual_ms > 0` 时至少为 1
 */
uint32_t time_util_to_wall_ms(uint32_t virtual_ms);

/**
 * @brief  测试用：丢掉已缓存的倍率，下次读取重新解析环境变量
 */
void time_util_reset_for_test(void);

/**
 * @brief  获取自系统启动以来的单调时间戳（毫秒）
 * @retval 当前毫秒时间戳。Host 开启倍率时按倍率放大，使固件超时与墙钟缩短成正比。
 */
uint64_t time_util_get_ms(void);

/**
 * @brief  计算两个时间戳之间的经过毫秒数
 * @param  start_ms  起始时间戳（由 time_util_get_ms() 获取）
 * @param  now_ms    当前时间戳（由 time_util_get_ms() 获取）
 * @retval 经过的毫秒数（业务间隔远小于 UINT32_MAX，返回值保持 uint32_t）
 * @note   必须用此函数计算时间差，禁止用 >= 直接比较两个时间戳。
 */
static inline uint32_t time_elapsed_ms(uint64_t start_ms, uint64_t now_ms)
{
    return (uint32_t)(now_ms - start_ms);
}

/**
 * @brief  填充 sem_timedwait 所需的绝对截止时间
 * @param  timeout_ms  相对超时毫秒数
 * @param  ts          输出参数，填充后可直接传给 sem_timedwait()
 * @note   内部使用 CLOCK_REALTIME（POSIX sem_timedwait 要求），
 *         不可用于时长测量（请用 time_util_get_ms()）。
 *         pthread_cond_timedwait 若已绑定 CLOCK_MONOTONIC，请用
 *         time_util_fill_monotonic_deadline()。
 */
void time_util_fill_deadline(uint32_t timeout_ms, struct timespec *ts);

/**
 * @brief  填充已绑定 CLOCK_MONOTONIC 的 pthread_cond_timedwait 截止时间
 * @param  timeout_ms  相对超时毫秒数
 * @param  ts          输出参数，填充后可直接传给 pthread_cond_timedwait()
 * @note   与 time_util_get_ms() 使用同一时钟，不受系统时间跳变影响。
 */
void time_util_fill_monotonic_deadline(uint32_t timeout_ms, struct timespec *ts);

#ifdef __cplusplus
}
#endif

#endif /* COMMON_TIME_UTIL_H */
