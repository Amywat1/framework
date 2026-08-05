/**
 * @file    test_event_bus.c
 * @brief   event_bus 单元测试
 * @author  HUWANGWEI
 * @date    2026-04-10
 */

#include "common/time_util.h"
#include "common/trace_context.h"
#include "runtime/event_bus/event_bus.h"
#include "runtime/event_bus/event_bus_config.h"
#include "wdf_test_spec.h"

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
static volatile uint64_t g_root_event_id;
static volatile uint64_t g_root_command_id;
static volatile uint64_t g_root_correlation_id;
static volatile uint64_t g_child_causation_id;
static volatile uint64_t g_child_command_id;

#define FIFO_LOG_MAX 8
static volatile uint32_t g_fifo_log[FIFO_LOG_MAX];
static volatile int      g_fifo_log_count;

static void reset_flags(void)
{
    g_h1_called           = 0;
    g_h2_called           = 0;
    g_h1_call_count       = 0;
    g_h1_param            = 0U;
    g_h1_timestamp        = 0U;
    g_fifo_log_count      = 0;
    g_root_event_id       = 0U;
    g_root_command_id     = 0U;
    g_root_correlation_id = 0U;
    g_child_causation_id  = 0U;
    g_child_command_id    = 0U;
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

static void trace_root_handler(const event_t *evt)
{
    g_root_event_id       = evt->event_id;
    g_root_command_id     = evt->trace.command_id;
    g_root_correlation_id = evt->trace.correlation_id;
    (void)event_publish(EVT_CLOUD_CONNECTED, 0U);
}

static void trace_child_handler(const event_t *evt)
{
    g_child_causation_id = evt->trace.causation_id;
    g_child_command_id   = evt->trace.command_id;
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

/* 分类别计数：不同类别事件各自计入自己的槽位 */
static void test_stats_count_by_category(void)
{
    event_bus_stats_t stats;
    pthread_t         tid;

    TEST_ASSERT_EQUAL_INT(SW_OK, event_bus_init());
    TEST_ASSERT_EQUAL_INT(SW_OK, event_subscribe(EVT_ALARM_TRIGGERED, handler1));
    tid = start_dispatch();

    TEST_ASSERT_EQUAL_INT(SW_OK, event_publish(EVT_ALARM_TRIGGERED, 1U));
    TEST_ASSERT_EQUAL_INT(SW_OK, event_publish(EVT_ALARM_CLEARED, 2U));
    TEST_ASSERT_EQUAL_INT(SW_OK, event_publish(EVT_CMD_ORDER, 3U));
    usleep(50000);

    TEST_ASSERT_EQUAL_INT(SW_OK, event_bus_get_stats(&stats));
    TEST_ASSERT_EQUAL_UINT(2U, stats.published_by_cat[EVT_CAT_ALARM]);
    TEST_ASSERT_EQUAL_UINT(1U, stats.published_by_cat[EVT_CAT_CMD]);
    TEST_ASSERT_EQUAL_UINT(0U, stats.published_by_cat[EVT_CAT_WASH]);
    TEST_ASSERT_EQUAL_UINT(2U, stats.dispatched_by_cat[EVT_CAT_ALARM]);
    TEST_ASSERT_EQUAL_UINT(1U, stats.dispatched_by_cat[EVT_CAT_CMD]);
    /* 分类别之和必须等于总计 */
    TEST_ASSERT_EQUAL_UINT(3U, stats.published_count);
    TEST_ASSERT_EQUAL_UINT(3U, stats.dispatched_count);

    stop_dispatch(tid);
}

/* 队列满时丢弃计数同样按类别归集 */
static void test_stats_dropped_by_category(void)
{
    event_bus_stats_t stats;

    TEST_ASSERT_EQUAL_INT(SW_OK, event_bus_init());

    /* 不启 dispatch，灌满普通队列 */
    for (unsigned i = 0; i < EVENT_BUS_QUEUE_SIZE + 3U; i++) {
        (void)event_publish(EVT_CMD_ORDER, i);
    }

    TEST_ASSERT_EQUAL_INT(SW_OK, event_bus_get_stats(&stats));
    TEST_ASSERT_EQUAL_UINT(3U, stats.dropped_count);
    TEST_ASSERT_EQUAL_UINT(3U, stats.dropped_by_cat[EVT_CAT_CMD]);
    TEST_ASSERT_EQUAL_UINT(EVENT_BUS_QUEUE_SIZE, stats.queue_peak_depth);
}

/* 慢 handler 被记入耗时统计并定位到事件类型 */
static void slow_handler(const event_t *evt)
{
    (void)evt;
    usleep((EVENT_BUS_SLOW_HANDLER_MS + 20U) * 1000U);
}

static void test_stats_records_slow_handler(void)
{
    event_bus_stats_t stats;
    pthread_t         tid;

    TEST_ASSERT_EQUAL_INT(SW_OK, event_bus_init());
    TEST_ASSERT_EQUAL_INT(SW_OK, event_subscribe(EVT_WASH_DONE, slow_handler));
    tid = start_dispatch();

    TEST_ASSERT_EQUAL_INT(SW_OK, event_publish(EVT_WASH_DONE, 0U));
    usleep((EVENT_BUS_SLOW_HANDLER_MS + 80U) * 1000U);

    TEST_ASSERT_EQUAL_INT(SW_OK, event_bus_get_stats(&stats));
    TEST_ASSERT_EQUAL_UINT(1U, stats.slow_handler_count);
    TEST_ASSERT_TRUE(stats.handler_max_ms >= EVENT_BUS_SLOW_HANDLER_MS);
    TEST_ASSERT_EQUAL_UINT16(EVT_WASH_DONE, stats.handler_max_type);
    TEST_ASSERT_TRUE(stats.dispatch_max_ms >= stats.handler_max_ms);

    stop_dispatch(tid);
}

/* 快 handler 不应被误判为慢 handler */
static void test_stats_fast_handler_not_flagged_slow(void)
{
    event_bus_stats_t stats;
    pthread_t         tid;

    TEST_ASSERT_EQUAL_INT(SW_OK, event_bus_init());
    TEST_ASSERT_EQUAL_INT(SW_OK, event_subscribe(EVT_ALARM_TRIGGERED, handler1));
    tid = start_dispatch();

    TEST_ASSERT_EQUAL_INT(SW_OK, event_publish(EVT_ALARM_TRIGGERED, 1U));
    usleep(50000);

    TEST_ASSERT_EQUAL_INT(SW_OK, event_bus_get_stats(&stats));
    TEST_ASSERT_EQUAL_UINT(0U, stats.slow_handler_count);

    stop_dispatch(tid);
}

static void test_trace_context_propagates_to_derived_event(void)
{
    trace_context_t context = {0};
    pthread_t       tid;

    TEST_ASSERT_EQUAL_INT(SW_OK, event_bus_init());
    TEST_ASSERT_EQUAL_INT(SW_OK, event_subscribe(EVT_CMD_ORDER, trace_root_handler));
    TEST_ASSERT_EQUAL_INT(SW_OK, event_subscribe(EVT_CLOUD_CONNECTED, trace_child_handler));
    context.command_id     = 42U;
    context.correlation_id = 84U;
    context.causation_id   = 21U;
    trace_context_set(&context);
    tid = start_dispatch();

    TEST_ASSERT_EQUAL_INT(SW_OK, event_publish(EVT_CMD_ORDER, 0U));
    trace_context_set(NULL);
    usleep(30000);

    TEST_ASSERT_NOT_EQUAL(0U, g_root_event_id);
    TEST_ASSERT_EQUAL_UINT64(42U, g_root_command_id);
    TEST_ASSERT_EQUAL_UINT64(84U, g_root_correlation_id);
    TEST_ASSERT_EQUAL_UINT64(g_root_event_id, g_child_causation_id);
    TEST_ASSERT_EQUAL_UINT64(42U, g_child_command_id);

    stop_dispatch(tid);
}

int main(void)
{
    time_util_init();
    UNITY_BEGIN();
    WDF_RUN_TEST(test_not_init_guard, "", "验证未初始化保护");
    WDF_RUN_TEST(test_publish_subscribe, "", "验证发布订阅");
    WDF_RUN_TEST(test_queue_full, "", "验证事件队列满时拒绝发布");
    WDF_RUN_TEST(test_multi_subscriber, "", "验证多个订阅者");
    WDF_RUN_TEST(test_fifo_order, "", "验证FIFO顺序");
    WDF_RUN_TEST(test_event_isolation, "", "验证事件隔离");
    WDF_RUN_TEST(test_subscribe_idempotent_and_stats, "", "验证订阅幂等并统计");
    WDF_RUN_TEST(test_shutdown_drains_queue, "", "验证关闭排空队列");
    WDF_RUN_TEST(test_stats_count_by_category, "", "验证统计数量按类别");
    WDF_RUN_TEST(test_stats_dropped_by_category, "", "验证统计丢弃按类别");
    WDF_RUN_TEST(test_stats_records_slow_handler, "", "验证统计记录慢速处理器");
    WDF_RUN_TEST(test_stats_fast_handler_not_flagged_slow, "", "验证统计快速处理器未标记慢速");
    WDF_RUN_TEST(test_trace_context_propagates_to_derived_event, "", "验证追踪上下文传播到派生事件");
    return UNITY_END();
}
