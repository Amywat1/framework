/**
 * @file    test_log_async.c
 * @brief   异步日志契约：队列满丢弃计数、trace 上下文随条目传递、未标记线程同步写出
 *
 * @note    用例顺序有依赖：丢弃用例须在 drain 线程启动前执行，
 *          drain 线程一经启动不可回收，后续用例共用它。
 */

#include "common/log.h"
#include "common/trace_context.h"
#include "wdf_test_spec.h"

#include <pthread.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

enum {
    LOG_ASYNC_EXTRA         = 3,
    LOG_ASYNC_MSG_MAX       = 64,
    LOG_ASYNC_WAIT_SLICES   = 2000,
    LOG_ASYNC_WAIT_SLICE_US = 1000,
};

static const trace_context_t k_probe_trace = {
    .wash_session_id = 11U,
    .command_id      = 22U,
    .correlation_id  = 33U,
    .causation_id    = 44U,
};

static pthread_mutex_t s_seen_lock = PTHREAD_MUTEX_INITIALIZER;
static bool            s_seen;
static char            s_seen_msg[LOG_ASYNC_MSG_MAX];
static trace_context_t s_seen_trace;
static pthread_t       s_seen_thread;
static const char     *s_expect_msg;
static bool            s_drain_started;

void setUp(void)
{
    (void)pthread_mutex_lock(&s_seen_lock);
    s_seen       = false;
    s_expect_msg = NULL;
    (void)pthread_mutex_unlock(&s_seen_lock);
    trace_context_set(NULL);
}

void tearDown(void)
{
    trace_context_set(NULL);
}

/** 只记录与期望文本一致的那条，忽略队列里残留的其它日志 */
static void probe_sink(sw_log_level_t level, const char *component, const char *fmt, va_list ap)
{
    char msg[LOG_ASYNC_MSG_MAX];

    (void)level;
    (void)component;
    (void)vsnprintf(msg, sizeof(msg), fmt, ap);

    (void)pthread_mutex_lock(&s_seen_lock);
    if ((s_expect_msg != NULL) && (strcmp(msg, s_expect_msg) == 0)) {
        (void)snprintf(s_seen_msg, sizeof(s_seen_msg), "%s", msg);
        s_seen_trace  = trace_context_get();
        s_seen_thread = pthread_self();
        s_seen        = true;
    }
    (void)pthread_mutex_unlock(&s_seen_lock);
}

static void expect_message(const char *msg)
{
    (void)pthread_mutex_lock(&s_seen_lock);
    s_expect_msg = msg;
    s_seen       = false;
    (void)pthread_mutex_unlock(&s_seen_lock);
}

static bool wait_seen(void)
{
    int  slices = 0;
    bool seen   = false;

    while (slices < LOG_ASYNC_WAIT_SLICES) {
        (void)pthread_mutex_lock(&s_seen_lock);
        seen = s_seen;
        (void)pthread_mutex_unlock(&s_seen_lock);
        if (seen) {
            break;
        }
        (void)usleep(LOG_ASYNC_WAIT_SLICE_US);
        slices++;
    }
    return seen;
}

static void start_drain_once(void)
{
    pthread_t tid;

    if (s_drain_started) {
        return;
    }
    TEST_ASSERT_EQUAL_INT(0, pthread_create(&tid, NULL, sw_log_drain_thread_fn, NULL));
    TEST_ASSERT_EQUAL_INT(0, pthread_detach(tid));
    s_drain_started = true;
}

static void test_async_drops_and_counts_when_spool_full(void)
{
    uint32_t before;
    unsigned i;

    sw_log_register_sink(probe_sink);
    sw_log_mark_thread_async();
    sw_log_enable_async();
    before = sw_log_dropped_count();

    for (i = 0U; i < (SW_LOG_SPOOL_CAP + (unsigned)LOG_ASYNC_EXTRA); i++) {
        sw_log_write(SW_LOG_INFO, "T", "fill-%u", i);
    }
    TEST_ASSERT_EQUAL_UINT32(before + (uint32_t)LOG_ASYNC_EXTRA, sw_log_dropped_count());
}

static void test_async_restores_caller_trace_context(void)
{
    char last_fill[LOG_ASYNC_MSG_MAX];

    sw_log_register_sink(probe_sink);
    sw_log_mark_thread_async();
    sw_log_enable_async();

    /* 等上一用例塞满的队列排空，否则探针会被丢弃 */
    (void)snprintf(last_fill, sizeof(last_fill), "fill-%u", SW_LOG_SPOOL_CAP - 1U);
    expect_message(last_fill);
    start_drain_once();
    TEST_ASSERT_TRUE(wait_seen());

    expect_message("trace-probe");
    trace_context_set(&k_probe_trace);
    sw_log_write(SW_LOG_WARN, "T", "trace-probe");
    trace_context_set(NULL);

    TEST_ASSERT_TRUE(wait_seen());
    TEST_ASSERT_EQUAL_STRING("trace-probe", s_seen_msg);
    TEST_ASSERT_EQUAL_INT(0, pthread_equal(s_seen_thread, pthread_self()));
    TEST_ASSERT_EQUAL_UINT64(k_probe_trace.wash_session_id, s_seen_trace.wash_session_id);
    TEST_ASSERT_EQUAL_UINT64(k_probe_trace.command_id, s_seen_trace.command_id);
    TEST_ASSERT_EQUAL_UINT64(k_probe_trace.correlation_id, s_seen_trace.correlation_id);
    TEST_ASSERT_EQUAL_UINT64(k_probe_trace.causation_id, s_seen_trace.causation_id);
}

static void *unmarked_writer(void *raw)
{
    bool *seen_sync = (bool *)raw;

    sw_log_write(SW_LOG_INFO, "T", "sync-probe");
    (void)pthread_mutex_lock(&s_seen_lock);
    *seen_sync = s_seen && (pthread_equal(s_seen_thread, pthread_self()) != 0);
    (void)pthread_mutex_unlock(&s_seen_lock);
    return NULL;
}

static void test_unmarked_thread_writes_synchronously(void)
{
    pthread_t tid;
    bool      seen_sync = false;

    sw_log_register_sink(probe_sink);
    sw_log_enable_async();
    start_drain_once();

    expect_message("sync-probe");
    TEST_ASSERT_EQUAL_INT(0, pthread_create(&tid, NULL, unmarked_writer, &seen_sync));
    TEST_ASSERT_EQUAL_INT(0, pthread_join(tid, NULL));
    TEST_ASSERT_TRUE(seen_sync);
}

int main(void)
{
    UNITY_BEGIN();
    WDF_RUN_TEST(test_async_drops_and_counts_when_spool_full, "", "验证异步队列满时丢弃并计数，不回退同步写出");
    WDF_RUN_TEST(test_async_restores_caller_trace_context, "", "验证异步日志在 drain 线程写 sink 时恢复调用方 trace 上下文");
    WDF_RUN_TEST(test_unmarked_thread_writes_synchronously, "", "验证未标记线程在异步开启后仍同步写出");
    return UNITY_END();
}
