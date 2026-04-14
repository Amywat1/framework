/**
 * @file    test_event_bus.c
 * @brief   event_bus 单元测试
 * @author  胡望伟
 * @date    2026-04-10
 *
 * @note    编译命令（在项目根目录执行）：
 *   gcc -std=c99 -Wall -Wextra \
 *       -I. \
 *       core/event_bus/event_bus.c \
 *       common/time_util.c \
 *       tests/unit/test_event_bus.c \
 *       -o test_event_bus -lpthread -lrt && ./test_event_bus
 */

#include "core/event_bus/event_bus.h"
#include "common/time_util.h"

#include <assert.h>
#include <stdio.h>
#include <unistd.h>     /* usleep */
#include <pthread.h>
#include <string.h>

/* -------------------------------------------------------------------------
 * 测试辅助：volatile 标志（C99 无 stdatomic，用 volatile + usleep 保证可见性）
 * ------------------------------------------------------------------------- */
static volatile int      g_h1_called;
static volatile int      g_h2_called;
static volatile int      g_h1_call_count;
static volatile uint32_t g_h1_param;
static volatile uint32_t g_h1_timestamp;

/* FIFO 顺序验证用 */
#define FIFO_LOG_MAX  8
static volatile uint32_t g_fifo_log[FIFO_LOG_MAX];
static volatile int      g_fifo_log_count;

static void reset_flags(void)
{
    g_h1_called = 0;
    g_h2_called = 0;
    g_h1_call_count = 0;
    g_h1_param = 0U;
    g_h1_timestamp = 0U;
    g_fifo_log_count = 0;
    memset((void *)g_fifo_log, 0, sizeof(g_fifo_log));
}

static void handler1(const event_t *evt)
{
    g_h1_called     = 1;
    g_h1_call_count++;
    g_h1_param      = evt->param;
    g_h1_timestamp  = evt->timestamp_ms;
}

static void handler2(const event_t *evt)
{
    (void)evt;
    g_h2_called = 1;
}

/* FIFO 顺序记录 handler */
static void fifo_handler(const event_t *evt)
{
    int idx = g_fifo_log_count; /* 读取快照避免多次访问 volatile */
    if (idx < FIFO_LOG_MAX)
    {
        g_fifo_log[idx] = evt->param;
        g_fifo_log_count = idx + 1;
    }
}

/* dispatch 线程入口（可被 pthread_cancel 取消，sem_wait 是取消点）*/
static void *dispatch_fn(void *arg)
{
    (void)arg;
    event_bus_dispatch_loop();
    return NULL;
}

/* 启动 dispatch 线程，返回 tid */
static pthread_t start_dispatch(void)
{
    pthread_t tid;
    pthread_create(&tid, NULL, dispatch_fn, NULL);
    return tid;
}

/* 停止 dispatch 线程 */
static void stop_dispatch(pthread_t tid)
{
    assert(event_bus_shutdown() == SW_OK);
    pthread_join(tid, NULL);
}

/* =========================================================================
 * 用例 1：publish → handler 被调用，param 和 timestamp 正确
 * ========================================================================= */
static void test_publish_subscribe(void)
{
    reset_flags();

    assert(event_bus_init() == SW_OK);
    assert(event_subscribe(EVT_ALARM_TRIGGERED, handler1) == SW_OK);

    pthread_t tid = start_dispatch();

    assert(event_publish(EVT_ALARM_TRIGGERED, 8100U) == SW_OK);
    usleep(30000); /* 等 30ms，dispatch 线程应已处理完 */

    assert(g_h1_called    == 1);
    assert(g_h1_param     == 8100U);
    assert(g_h1_timestamp  > 0U);   /* timestamp 由总线自动填充 */

    stop_dispatch(tid);
    printf("PASS: test_publish_subscribe\n");
}

/* =========================================================================
 * 用例 2：队列填满后 publish 返回 SW_ERR_OVERFLOW
 * ========================================================================= */
static void test_queue_full(void)
{
    event_bus_stats_t stats;
    assert(event_bus_init() == SW_OK);

    /* 不启动 dispatch 线程 —— 让队列积压 */
    sw_err_t rc = SW_OK;
    for (int i = 0; i < (int)EVENT_BUS_QUEUE_SIZE + 5; i++)
    {
        rc = event_publish(EVT_CMD_ORDER, (uint32_t)i);
    }
    /* 前 EVENT_BUS_QUEUE_SIZE 次应成功，之后应返回 SW_ERR_OVERFLOW */
    assert(rc == SW_ERR_OVERFLOW);
    assert(event_bus_get_stats(&stats) == SW_OK);
    assert(stats.queue_depth == EVENT_BUS_QUEUE_SIZE);
    assert(stats.queue_peak_depth == EVENT_BUS_QUEUE_SIZE);
    assert(stats.dropped_count == 5U);

    printf("PASS: test_queue_full\n");
}

/* =========================================================================
 * 用例 3：同一事件的多个 subscriber 各自收到事件
 * ========================================================================= */
static void test_multi_subscriber(void)
{
    reset_flags();

    assert(event_bus_init() == SW_OK);
    assert(event_subscribe(EVT_SAFETY_LOCKOUT, handler1) == SW_OK);
    assert(event_subscribe(EVT_SAFETY_LOCKOUT, handler2) == SW_OK);

    pthread_t tid = start_dispatch();

    assert(event_publish(EVT_SAFETY_LOCKOUT, 0U) == SW_OK);
    usleep(30000);

    assert(g_h1_called == 1);
    assert(g_h2_called == 1);

    stop_dispatch(tid);
    printf("PASS: test_multi_subscriber\n");
}

/* =========================================================================
 * 用例 4：多个事件按 FIFO 顺序分发，且 param 顺序严格一致
 * ========================================================================= */
static void test_fifo_order(void)
{
    reset_flags();

    assert(event_bus_init() == SW_OK);
    assert(event_subscribe(EVT_CMD_ORDER, fifo_handler) == SW_OK);

    pthread_t tid = start_dispatch();

    event_publish(EVT_CMD_ORDER, 10U);
    event_publish(EVT_CMD_ORDER, 20U);
    event_publish(EVT_CMD_ORDER, 30U);
    usleep(50000); /* 给 dispatch 足够时间处理 3 个事件 */

    /* 验证数量 */
    assert(g_fifo_log_count == 3);
    /* 验证顺序 —— FIFO 要求与入队顺序完全一致 */
    assert(g_fifo_log[0] == 10U);
    assert(g_fifo_log[1] == 20U);
    assert(g_fifo_log[2] == 30U);

    stop_dispatch(tid);
    printf("PASS: test_fifo_order (order: %u %u %u)\n",
           (unsigned)g_fifo_log[0],
           (unsigned)g_fifo_log[1],
           (unsigned)g_fifo_log[2]);
}

/* =========================================================================
 * 用例 5：不同事件类型的 subscriber 互不干扰
 * ========================================================================= */
static void test_event_isolation(void)
{
    reset_flags();

    assert(event_bus_init() == SW_OK);
    assert(event_subscribe(EVT_ALARM_TRIGGERED, handler1) == SW_OK);
    assert(event_subscribe(EVT_CLOUD_CONNECTED, handler2) == SW_OK);

    pthread_t tid = start_dispatch();

    event_publish(EVT_ALARM_TRIGGERED, 8010U);
    usleep(20000);

    assert(g_h1_called == 1);
    assert(g_h2_called == 0); /* CLOUD_CONNECTED 没发，handler2 不应被调用 */

    stop_dispatch(tid);
    printf("PASS: test_event_isolation\n");
}

/* =========================================================================
 * 用例 6：未初始化时 subscribe / publish 应返回 SW_ERR_NOT_INIT
 * ========================================================================= */
static void test_not_init_guard(void)
{
    assert(event_subscribe(EVT_ALARM_TRIGGERED, handler1) == SW_ERR_NOT_INIT);
    assert(event_publish(EVT_ALARM_TRIGGERED, 1U) == SW_ERR_NOT_INIT);
    assert(event_bus_shutdown() == SW_ERR_NOT_INIT);

    printf("PASS: test_not_init_guard\n");
}

/* =========================================================================
 * 用例 7：重复订阅幂等，统计值正确
 * ========================================================================= */
static void test_subscribe_idempotent_and_stats(void)
{
    event_bus_stats_t stats;

    reset_flags();
    assert(event_bus_init() == SW_OK);
    assert(event_subscribe(EVT_ALARM_TRIGGERED, handler1) == SW_OK);
    assert(event_subscribe(EVT_ALARM_TRIGGERED, handler1) == SW_OK);
    assert(event_subscribe(EVT_ALARM_TRIGGERED, handler2) == SW_OK);

    assert(event_bus_get_stats(&stats) == SW_OK);
    assert(stats.subscribe_count == 2U);

    pthread_t tid = start_dispatch();
    assert(event_publish(EVT_ALARM_TRIGGERED, 9001U) == SW_OK);
    usleep(30000);
    assert(g_h1_called == 1);
    assert(g_h2_called == 1);
    assert(g_h1_call_count == 1);

    stop_dispatch(tid);
    printf("PASS: test_subscribe_idempotent_and_stats\n");
}

/* =========================================================================
 * 用例 8：shutdown 后分发线程正常退出，且统计可读
 * ========================================================================= */
static void test_shutdown_drains_queue(void)
{
    event_bus_stats_t stats;

    reset_flags();
    assert(event_bus_init() == SW_OK);
    assert(event_subscribe(EVT_CMD_ORDER, fifo_handler) == SW_OK);

    pthread_t tid = start_dispatch();
    assert(event_publish(EVT_CMD_ORDER, 1U) == SW_OK);
    assert(event_publish(EVT_CMD_ORDER, 2U) == SW_OK);
    assert(event_publish(EVT_CMD_ORDER, 3U) == SW_OK);

    stop_dispatch(tid);

    assert(g_fifo_log_count == 3);
    assert(g_fifo_log[0] == 1U);
    assert(g_fifo_log[1] == 2U);
    assert(g_fifo_log[2] == 3U);

    assert(event_bus_get_stats(&stats) == SW_OK);
    assert(stats.published_count == 3U);
    assert(stats.dispatched_count == 3U);
    assert(stats.queue_depth == 0U);

    printf("PASS: test_shutdown_drains_queue\n");
}

/* =========================================================================
 * main
 * ========================================================================= */
int main(void)
{
    printf("=== event_bus unit tests ===\n");
    time_util_init();

    test_not_init_guard();
    test_publish_subscribe();
    test_queue_full();
    test_multi_subscriber();
    test_fifo_order();
    test_event_isolation();
    test_subscribe_idempotent_and_stats();
    test_shutdown_drains_queue();

    printf("=== All 8 tests PASSED ===\n");
    return 0;
}
