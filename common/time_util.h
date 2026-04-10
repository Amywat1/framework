/**
 * @file    time_util.h
 * @brief   单调时钟工具（毫秒级时间戳）
 * @author  胡望伟
 * @date    2026-04-10
 *
 * @note    基于 CLOCK_MONOTONIC，不受系统时间调整影响。
 *          time_util_init() 须在 event_bus_init() 之前调用（见 bootstrap 顺序）。
 */

#ifndef TIME_UTIL_H
#define TIME_UTIL_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

/**
 * @brief  初始化时钟工具（Linux 上为空操作，保留供跨平台扩展）
 */
void time_util_init(void);

/**
 * @brief  获取自系统启动以来的单调时间戳（毫秒）
 * @retval 当前毫秒时间戳（约 49.7 天溢出，嵌入式场景可接受）
 */
uint32_t time_util_get_ms(void);

#ifdef __cplusplus
}
#endif

#endif /* TIME_UTIL_H */
