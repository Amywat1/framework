/**
 * @file    time_util.c
 * @brief   单调时钟工具实现（Linux CLOCK_MONOTONIC）
 * @author  HUWANGWEI
 * @date    2026-04-10
 */

#include "framework/common/time_util.h"
#include <time.h>

void time_util_init(void)
{
    /* Linux CLOCK_MONOTONIC 无需初始化，保留此函数供跨平台扩展 */
}

uint64_t time_util_get_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000ULL + (uint64_t)ts.tv_nsec / 1000000ULL;
}
