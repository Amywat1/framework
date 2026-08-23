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
 * @brief  初始化时钟工具（Linux 上为空操作，保留供跨平台扩展）
 */
void time_util_init(void);

/**
 * @brief  获取自系统启动以来的单调时间戳（毫秒）
 * @retval 当前毫秒时间戳（uint64_t，无实际溢出风险）
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
