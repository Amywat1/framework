/**
 * @file    test_event_bus.c
 * @brief   event_bus 单元测试
 * @author  胡望伟
 * @date    2026-04-10
 *
 * @note    编译命令（在项目根目录执行）：
 *   gcc -std=c99 -Wall -Wextra \
 *       -I. -I./src \
 *       src/core/event_bus/event_bus.c \
 *       common/time_util.c \
 *       src/tests/unit/test_event_bus.c \
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
static volatile int g_h1_called;
static volatile int g_h2_called;
static volatile int g_h1_call_count;
static volatile uint32_t g_h1_param;
static volatile uint32_t g_h1_timestamp;

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

/* dispatch 线程入口（可被 pthread_cancel 取消，sem_wait 是取消点）*/
static void *dispatch_fn(void *arg)
{
    (void)arg;
    pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL);
    pthread_setcanceltype(PTHREAD_CANCEL_DEFERRED, NULL);
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
    pthread_cancel(tid);
    pthread_join(tid, NULL);
}

/* =========================================================================
 * 用例 1：publish → handler 被调用，param 和 timestamp 正确
 * ========================================================================= */
static void test_publish_subscribe(void)
{
    g_h1_called    = 0;
    g_h1_param     = 0;
    g_h1_timestamp = 0;

    event_bus_init();
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
    event_bus_init();

    /* 不启动 dispatch 线程 —— 让队列积压 */
    sw_err_t rc = SW_OK;
    for (int i = 0; i < (int)EVENT_BUS_QUEUE_SIZE + 5; i++)
    {
        rc = event_publish(EVT_HW_ENCODER_TICK, (uint32_t)i);
    }
    /* 前 EVENT_BUS_QUEUE_SIZE 次应成功，之后应返回 SW_ERR_OVERFLOW */
    assert(rc == SW_ERR_OVERFLOW);

    printf("PASS: test_queue_full\n");
}

/* =========================================================================
 * 用例 3：同一事件的多个 subscriber 各自收到事件
 * ========================================================================= */
static void test_multi_subscriber(void)
{
    g_h1_called = 0;
    g_h2_called = 0;

    event_bus_init();
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
 * 用例 4：多个事件按 FIFO 顺序分发，全部到达
 * ========================================================================= */
static void test_fifo_order(void)
{
    g_h1_call_count = 0;

    event_bus_init();
    assert(event_subscribe(EVT_CMD_ORDER, handler1) == SW_OK);

    pthread_t tid = start_dispatch();

    event_publish(EVT_CMD_ORDER, 1U);
    event_publish(EVT_CMD_ORDER, 2U);
    event_publish(EVT_CMD_ORDER, 3U);
    usleep(50000); /* 给 dispatch 足够时间处理 3 个事件 */

    assert(g_h1_call_count == 3);

    stop_dispatch(tid);
    printf("PASS: test_fifo_order (3 events received)\n");
}

/* =========================================================================
 * 用例 5：不同事件类型的 subscriber 互不干扰
 * ========================================================================= */
static void test_event_isolation(void)
{
    g_h1_called = 0;
    g_h2_called = 0;

    event_bus_init();
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
 * main
 * ========================================================================= */
int main(void)
{
    printf("=== event_bus unit tests ===\n");
    time_util_init();

    test_publish_subscribe();
    test_queue_full();
    test_multi_subscriber();
    test_fifo_order();
    test_event_isolation();

    printf("=== All 5 tests PASSED ===\n");
    return 0;
}
