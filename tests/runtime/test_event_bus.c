/**
 * @file    test_event_bus.c
 * @brief   event_bus 单元测试
 * @author  HUWANGWEI
 * @date    2026-04-10
 */

#include "common/time_util.h"
#include "runtime/event_bus/event_bus_config.h"
#include "runtime/event_bus/event_bus.h"
#include "unity.h"

#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

/* -------------------------------------------------------------------------
 * 测试辅助：volatile 标志（C99 无 stdatomic，用 volatile + usleep 保证可见性）
 * ------------------------------------------------------------------------- */
static volatile int      g_h1_called;
static volatile int      g_h2_called;
static volatile int      g_h1_call_count;
static volatile uint32_t g_h1_param;
static volatile uint64_t g_h1_timestamp;

#define FIFO_LOG_MAX 8
static volatile uint32_t g_fifo_log[FIFO_LOG_MAX];
static volatile int      g_fifo_log_count;

static void reset_flags(void)
{
    g_h1_called      = 0;
    g_h2_called      = 0;
    g_h1_call_count  = 0;
    g_h1_param       = 0U;
    g_h1_timestamp   = 0U;
    g_fifo_log_count = 0;
    memset((void *)g_fifo_log, 0, sizeof(g_fifo_log));
}

static void handler1(const event_t *evt)
{
    g_h1_called = 1;
    g_h1_call_count++;
    g_h1_param     = evt->param;
    g_h1_timestamp = evt->timestamp_ms;
}

static void handler2(const event_t *evt)
{
    (void)evt;
    g_h2_called = 1;
}

static void fifo_handler(const event_t *evt)
{
    int idx = g_fifo_log_count;
    if (idx < FIFO_LOG_MAX) {
        g_fifo_log[idx]  = evt->param;
        g_fifo_log_count = idx + 1;
    }
}

static void *dispatch_fn(void *arg)
{
    (void)arg;
    event_bus_dispatch_loop();
    return NULL;
}

static pthread_t start_dispatch(void)
{
    pthread_t tid;
    pthread_create(&tid, NULL, dispatch_fn, NULL);
    return tid;
}

static void stop_dispatch(pthread_t tid)
{
    TEST_ASSERT_EQUAL_INT(SW_OK, event_bus_shutdown());
    pthread_join(tid, NULL);
}

void setUp(void)
{
    reset_flags();
}

void tearDown(void)
{
}

static void test_publish_subscribe(void)
{
    TEST_ASSERT_EQUAL_INT(SW_OK, event_bus_init());
    TEST_ASSERT_EQUAL_INT(SW_OK, event_subscribe(EVT_ALARM_TRIGGERED, handler1));

    pthread_t tid = start_dispatch();

    TEST_ASSERT_EQUAL_INT(SW_OK, event_publish(EVT_ALARM_TRIGGERED, 8100U));
    usleep(30000);

    TEST_ASSERT_EQUAL_INT(1, g_h1_called);
    TEST_ASSERT_EQUAL_UINT32(8100U, g_h1_param);
    TEST_ASSERT_TRUE(g_h1_timestamp > 0U);

    stop_dispatch(tid);
}

static void test_queue_full(void)
{
    event_bus_stats_t stats;

    TEST_ASSERT_EQUAL_INT(SW_OK, event_bus_init());

    sw_err_t rc = SW_OK;
    for (int i = 0; i < (int)EVENT_BUS_QUEUE_SIZE + 5; i++) {
        rc = event_publish(EVT_CMD_ORDER, (uint32_t)i);
    }
    TEST_ASSERT_EQUAL_INT(SW_ERR_OVERFLOW, rc);
    TEST_ASSERT_EQUAL_INT(SW_OK, event_bus_get_stats(&stats));
    TEST_ASSERT_EQUAL_UINT(EVENT_BUS_QUEUE_SIZE, stats.queue_depth);
    TEST_ASSERT_EQUAL_UINT(EVENT_BUS_QUEUE_SIZE, stats.queue_peak_depth);
    TEST_ASSERT_EQUAL_UINT(5U, stats.dropped_count);
}

static void test_multi_subscriber(void)
{
    TEST_ASSERT_EQUAL_INT(SW_OK, event_bus_init());
    TEST_ASSERT_EQUAL_INT(SW_OK, event_subscribe(EVT_SAFETY_LOCKOUT, handler1));
    TEST_ASSERT_EQUAL_INT(SW_OK, event_subscribe(EVT_SAFETY_LOCKOUT, handler2));

    pthread_t tid = start_dispatch();

    TEST_ASSERT_EQUAL_INT(SW_OK, event_publish(EVT_SAFETY_LOCKOUT, 0U));
    usleep(30000);

    TEST_ASSERT_EQUAL_INT(1, g_h1_called);
    TEST_ASSERT_EQUAL_INT(1, g_h2_called);

    stop_dispatch(tid);
}

static void test_fifo_order(void)
{
    TEST_ASSERT_EQUAL_INT(SW_OK, event_bus_init());
    TEST_ASSERT_EQUAL_INT(SW_OK, event_subscribe(EVT_CMD_ORDER, fifo_handler));

    pthread_t tid = start_dispatch();

    event_publish(EVT_CMD_ORDER, 10U);
    event_publish(EVT_CMD_ORDER, 20U);
    event_publish(EVT_CMD_ORDER, 30U);
    usleep(50000);

    TEST_ASSERT_EQUAL_INT(3, g_fifo_log_count);
    TEST_ASSERT_EQUAL_UINT32(10U, g_fifo_log[0]);
    TEST_ASSERT_EQUAL_UINT32(20U, g_fifo_log[1]);
    TEST_ASSERT_EQUAL_UINT32(30U, g_fifo_log[2]);

    stop_dispatch(tid);
}

static void test_event_isolation(void)
{
    TEST_ASSERT_EQUAL_INT(SW_OK, event_bus_init());
    TEST_ASSERT_EQUAL_INT(SW_OK, event_subscribe(EVT_ALARM_TRIGGERED, handler1));
    TEST_ASSERT_EQUAL_INT(SW_OK, event_subscribe(EVT_CLOUD_CONNECTED, handler2));

    pthread_t tid = start_dispatch();

    event_publish(EVT_ALARM_TRIGGERED, 8010U);
    usleep(20000);

    TEST_ASSERT_EQUAL_INT(1, g_h1_called);
    TEST_ASSERT_EQUAL_INT(0, g_h2_called);

    stop_dispatch(tid);
}

static void test_not_init_guard(void)
{
    TEST_ASSERT_EQUAL_INT(SW_ERR_NOT_INIT, event_subscribe(EVT_ALARM_TRIGGERED, handler1));
    TEST_ASSERT_EQUAL_INT(SW_ERR_NOT_INIT, event_publish(EVT_ALARM_TRIGGERED, 1U));
    TEST_ASSERT_EQUAL_INT(SW_ERR_NOT_INIT, event_bus_shutdown());
}

static void test_subscribe_idempotent_and_stats(void)
{
    event_bus_stats_t stats;

    TEST_ASSERT_EQUAL_INT(SW_OK, event_bus_init());
    TEST_ASSERT_EQUAL_INT(SW_OK, event_subscribe(EVT_ALARM_TRIGGERED, handler1));
    TEST_ASSERT_EQUAL_INT(SW_OK, event_subscribe(EVT_ALARM_TRIGGERED, handler1));
    TEST_ASSERT_EQUAL_INT(SW_OK, event_subscribe(EVT_ALARM_TRIGGERED, handler2));

    TEST_ASSERT_EQUAL_INT(SW_OK, event_bus_get_stats(&stats));
    TEST_ASSERT_EQUAL_UINT(2U, stats.subscribe_count);

    pthread_t tid = start_dispatch();
    TEST_ASSERT_EQUAL_INT(SW_OK, event_publish(EVT_ALARM_TRIGGERED, 9001U));
    usleep(30000);
    TEST_ASSERT_EQUAL_INT(1, g_h1_called);
    TEST_ASSERT_EQUAL_INT(1, g_h2_called);
    TEST_ASSERT_EQUAL_INT(1, g_h1_call_count);

    stop_dispatch(tid);
}

static void test_shutdown_drains_queue(void)
{
    event_bus_stats_t stats;

    TEST_ASSERT_EQUAL_INT(SW_OK, event_bus_init());
    TEST_ASSERT_EQUAL_INT(SW_OK, event_subscribe(EVT_CMD_ORDER, fifo_handler));

    pthread_t tid = start_dispatch();
    TEST_ASSERT_EQUAL_INT(SW_OK, event_publish(EVT_CMD_ORDER, 1U));
    TEST_ASSERT_EQUAL_INT(SW_OK, event_publish(EVT_CMD_ORDER, 2U));
    TEST_ASSERT_EQUAL_INT(SW_OK, event_publish(EVT_CMD_ORDER, 3U));

    stop_dispatch(tid);

    TEST_ASSERT_EQUAL_INT(3, g_fifo_log_count);
    TEST_ASSERT_EQUAL_UINT32(1U, g_fifo_log[0]);
    TEST_ASSERT_EQUAL_UINT32(2U, g_fifo_log[1]);
    TEST_ASSERT_EQUAL_UINT32(3U, g_fifo_log[2]);

    TEST_ASSERT_EQUAL_INT(SW_OK, event_bus_get_stats(&stats));
    TEST_ASSERT_EQUAL_UINT(3U, stats.published_count);
    TEST_ASSERT_EQUAL_UINT(3U, stats.dispatched_count);
    TEST_ASSERT_EQUAL_UINT(0U, stats.queue_depth);
}

int main(void)
{
    time_util_init();
    UNITY_BEGIN();
    RUN_TEST(test_not_init_guard);
    RUN_TEST(test_publish_subscribe);
    RUN_TEST(test_queue_full);
    RUN_TEST(test_multi_subscriber);
    RUN_TEST(test_fifo_order);
    RUN_TEST(test_event_isolation);
    RUN_TEST(test_subscribe_idempotent_and_stats);
    RUN_TEST(test_shutdown_drains_queue);
    return UNITY_END();
}
