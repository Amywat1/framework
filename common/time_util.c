/**
 * @file    time_util.c
 * @brief   单调时钟工具实现（Linux CLOCK_MONOTONIC）
 * @author  HUWANGWEI
 * @date    2026-04-10
 */

#include "common/time_util.h"

#include "common/log.h"

#include <stdbool.h>
#include <stdlib.h>
#include <time.h>

#ifndef WDF_ALLOW_TIME_SCALE
#define WDF_ALLOW_TIME_SCALE 0
#endif

static uint32_t s_scale = 1U;
static bool     s_ready = false;

/**
 * @brief  解析 `WDF_TIME_SCALE`；非法或未允许时返回 1
 * @note   只收 1 / 2 / 5，须整除所有被缩短的周期（控制环 10 ms、报警轮询 30 ms），
 *         避免 4、8 整数截断后与虚拟时钟偏离。非法非空值回落到 1 并打日志。
 */
static uint32_t parse_scale(void)
{
    const char *text;
    char       *end = NULL;
    unsigned long value;

#if !WDF_ALLOW_TIME_SCALE
    return 1U;
#endif

    text = getenv("WDF_TIME_SCALE");
    if ((text == NULL) || (text[0] == '\0')) {
        return 1U;
    }

    value = strtoul(text, &end, 10);
    if ((end == text) || (*end != '\0')
        || ((value != 1UL) && (value != 2UL) && (value != 5UL))) {
        LOG_WARN("非法 WDF_TIME_SCALE=%s，按 1 运行（仅允许 1/2/5）", text);
        return 1U;
    }
    return (uint32_t)value;
}

static void ensure_ready(void)
{
    if (!s_ready) {
        s_scale = parse_scale();
        s_ready = true;
    }
}

void time_util_init(void)
{
    s_ready = false;
    ensure_ready();
}

void time_util_reset_for_test(void)
{
    s_scale = 1U;
    s_ready = false;
}

uint32_t time_util_scale(void)
{
    ensure_ready();
    return s_scale;
}

uint32_t time_util_to_wall_ms(uint32_t virtual_ms)
{
    uint32_t scale;

    ensure_ready();
    scale = s_scale;
    if ((virtual_ms == 0U) || (scale <= 1U)) {
        return virtual_ms;
    }
    if (virtual_ms < scale) {
        return 1U;
    }
    return virtual_ms / scale;
}

uint64_t time_util_get_ms(void)
{
    struct timespec ts;
    uint64_t        wall_ms;

    ensure_ready();
    clock_gettime(CLOCK_MONOTONIC, &ts);
    wall_ms = (uint64_t)ts.tv_sec * 1000ULL + (uint64_t)ts.tv_nsec / 1000000ULL;
    return wall_ms * (uint64_t)s_scale;
}

static void time_util_fill_deadline_clock(clockid_t clock, uint32_t timeout_ms, struct timespec *ts)
{
    uint32_t wall_ms;

    clock_gettime(clock, ts);
    wall_ms = time_util_to_wall_ms(timeout_ms);
    ts->tv_sec += (time_t)(wall_ms / 1000U);
    ts->tv_nsec += (long)(wall_ms % 1000U) * 1000000L;
    if (ts->tv_nsec >= 1000000000L) {
        ts->tv_sec += 1;
        ts->tv_nsec -= 1000000000L;
    }
}

void time_util_fill_deadline(uint32_t timeout_ms, struct timespec *ts)
{
    time_util_fill_deadline_clock(CLOCK_REALTIME, timeout_ms, ts);
}

void time_util_fill_monotonic_deadline(uint32_t timeout_ms, struct timespec *ts)
{
    time_util_fill_deadline_clock(CLOCK_MONOTONIC, timeout_ms, ts);
}
