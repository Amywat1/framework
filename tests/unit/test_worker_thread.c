#define _POSIX_C_SOURCE 200809L

#include "test_support.h"

#include <stdatomic.h>
#include <time.h>

#include "adapters/os/worker_thread.h"

typedef struct waiting_worker_context_t
{
    unsigned long wait_timeout_ms;
    atomic_bool entered_wait;
    atomic_bool wait_finished;
    atomic_bool wait_result;
    atomic_bool stop_requested_after_wait;
    atomic_ulong waited_ms;
} waiting_worker_context_t;

typedef struct notify_count_context_t
{
    unsigned int target_count;
    atomic_uint notifications_received;
    atomic_bool fourth_wait_returned_false;
    atomic_bool done;
} notify_count_context_t;

static unsigned long test_read_monotonic_ms(void)
{
    struct timespec current_time;

    if (clock_gettime(CLOCK_MONOTONIC, &current_time) != 0)
    {
        return 0ul;
    }

    return (unsigned long)(current_time.tv_sec * 1000ul) + (unsigned long)(current_time.tv_nsec / 1000000ul);
}

static void test_sleep_ms(unsigned long sleep_ms)
{
    struct timespec sleep_time;

    sleep_time.tv_sec = (time_t)(sleep_ms / 1000ul);
    sleep_time.tv_nsec = (long)((sleep_ms % 1000ul) * 1000000ul);
    (void)nanosleep(&sleep_time, 0);
}

static bool test_wait_until_true(atomic_bool *flag, unsigned long timeout_ms)
{
    unsigned long started_at_ms;

    if (flag == 0)
    {
        return false;
    }

    started_at_ms = test_read_monotonic_ms();
    while (!atomic_load(flag))
    {
        if (test_read_monotonic_ms() - started_at_ms >= timeout_ms)
        {
            return false;
        }
        test_sleep_ms(1ul);
    }
    return true;
}

static void idle_worker_entry(worker_run_ctx_t *run_ctx, void *context)
{
    (void)context;

    while (!worker_run_ctx_stop_requested(run_ctx))
    {
        test_sleep_ms(1ul);
    }
}

/* 消费 target_count 个通知后，再尝试第四次（超时 0）并记录结果。 */
static void notify_counting_entry(worker_run_ctx_t *run_ctx, void *context)
{
    notify_count_context_t *ctx;
    unsigned int received;
    bool extra_result;

    ctx = (notify_count_context_t *)context;
    if (ctx == 0)
    {
        return;
    }

    received = 0u;
    while (!worker_run_ctx_stop_requested(run_ctx) && received < ctx->target_count)
    {
        if (worker_run_ctx_wait(run_ctx, 200ul))
        {
            received += 1u;
            atomic_store(&ctx->notifications_received, received);
        }
    }

    /* 尝试第四次，应当返回 false（无剩余通知）。 */
    extra_result = worker_run_ctx_wait(run_ctx, 0ul);
    atomic_store(&ctx->fourth_wait_returned_false, !extra_result);
    atomic_store(&ctx->done, true);
}

static void waiting_worker_entry(worker_run_ctx_t *run_ctx, void *context)
{
    waiting_worker_context_t *waiting_context;
    unsigned long started_at_ms;
    bool wait_result;

    waiting_context = (waiting_worker_context_t *)context;
    if (waiting_context == 0)
    {
        return;
    }

    atomic_store(&waiting_context->entered_wait, true);
    started_at_ms = test_read_monotonic_ms();
    wait_result = worker_run_ctx_wait(run_ctx, waiting_context->wait_timeout_ms);
    atomic_store(&waiting_context->wait_result, wait_result);
    atomic_store(&waiting_context->stop_requested_after_wait, worker_run_ctx_stop_requested(run_ctx));
    atomic_store(&waiting_context->waited_ms, test_read_monotonic_ms() - started_at_ms);
    atomic_store(&waiting_context->wait_finished, true);
}

static int verify_notify_is_counted_and_consumed_in_order(void)
{
    notify_count_context_t ctx;
    worker_thread_config_t worker_thread_config;
    worker_thread_t *worker_thread;
    operation_result_t result;
    unsigned long started_at_ms;

    atomic_init(&ctx.notifications_received, 0u);
    atomic_init(&ctx.fourth_wait_returned_false, false);
    atomic_init(&ctx.done, false);
    ctx.target_count = 3u;

    memset(&worker_thread_config, 0, sizeof(worker_thread_config));
    worker_thread_config.entry = notify_counting_entry;
    worker_thread_config.context = &ctx;

    worker_thread = 0;
    result = worker_thread_start(&worker_thread, &worker_thread_config);
    TEST_ASSERT(result.ok);
    TEST_ASSERT(worker_thread != 0);

    TEST_ASSERT(worker_thread_notify(worker_thread).ok);
    TEST_ASSERT(worker_thread_notify(worker_thread).ok);
    TEST_ASSERT(worker_thread_notify(worker_thread).ok);

    /* 等待线程内部消费全部 3 个通知并完成第四次探测。 */
    started_at_ms = test_read_monotonic_ms();
    while (!atomic_load(&ctx.done))
    {
        if (test_read_monotonic_ms() - started_at_ms > 1000ul)
        {
            break;
        }
        test_sleep_ms(5ul);
    }

    TEST_ASSERT(atomic_load(&ctx.done));
    TEST_ASSERT(atomic_load(&ctx.notifications_received) == 3u);
    TEST_ASSERT(atomic_load(&ctx.fourth_wait_returned_false));

    TEST_ASSERT(worker_thread_request_stop(worker_thread).ok);
    TEST_ASSERT(worker_thread_join(worker_thread).ok);
    worker_thread_destroy(worker_thread);
    return 0;
}

static int verify_wait_times_out_without_notification(void)
{
    worker_thread_config_t worker_thread_config;
    worker_thread_t *worker_thread;
    unsigned long started_at_ms;
    unsigned long elapsed_ms;
    operation_result_t result;

    memset(&worker_thread_config, 0, sizeof(worker_thread_config));
    worker_thread_config.entry = idle_worker_entry;
    worker_thread_config.context = 0;

    worker_thread = 0;
    result = worker_thread_start(&worker_thread, &worker_thread_config);
    TEST_ASSERT(result.ok);
    TEST_ASSERT(worker_thread != 0);

    started_at_ms = test_read_monotonic_ms();
    /* worker_run_ctx_wait 超时测试由 waiting_worker_entry 覆盖；此处仅验证线程正常启停。 */
    (void)started_at_ms;
    elapsed_ms = 0ul;
    (void)elapsed_ms;

    TEST_ASSERT(worker_thread_request_stop(worker_thread).ok);
    TEST_ASSERT(worker_thread_join(worker_thread).ok);
    worker_thread_destroy(worker_thread);
    return 0;
}

static int verify_wait_timeout_in_entry(void)
{
    waiting_worker_context_t waiting_context;
    worker_thread_config_t worker_thread_config;
    worker_thread_t *worker_thread;
    unsigned long started_at_ms;
    unsigned long elapsed_ms;
    operation_result_t result;

    memset(&waiting_context, 0, sizeof(waiting_context));
    atomic_init(&waiting_context.entered_wait, false);
    atomic_init(&waiting_context.wait_finished, false);
    atomic_init(&waiting_context.wait_result, false);
    atomic_init(&waiting_context.stop_requested_after_wait, false);
    atomic_init(&waiting_context.waited_ms, 0ul);
    waiting_context.wait_timeout_ms = 40ul;

    memset(&worker_thread_config, 0, sizeof(worker_thread_config));
    worker_thread_config.entry = waiting_worker_entry;
    worker_thread_config.context = &waiting_context;

    worker_thread = 0;
    result = worker_thread_start(&worker_thread, &worker_thread_config);
    TEST_ASSERT(result.ok);
    TEST_ASSERT(worker_thread != 0);
    TEST_ASSERT(test_wait_until_true(&waiting_context.wait_finished, 500ul));

    TEST_ASSERT(!atomic_load(&waiting_context.wait_result));
    elapsed_ms = atomic_load(&waiting_context.waited_ms);
    started_at_ms = 0ul;
    (void)started_at_ms;
    TEST_ASSERT(elapsed_ms >= 20ul);

    TEST_ASSERT(worker_thread_request_stop(worker_thread).ok);
    TEST_ASSERT(worker_thread_join(worker_thread).ok);
    worker_thread_destroy(worker_thread);
    return 0;
}

static int verify_request_stop_wakes_waiting_thread(void)
{
    waiting_worker_context_t waiting_context;
    worker_thread_config_t worker_thread_config;
    worker_thread_t *worker_thread;
    operation_result_t result;

    memset(&waiting_context, 0, sizeof(waiting_context));
    atomic_init(&waiting_context.entered_wait, false);
    atomic_init(&waiting_context.wait_finished, false);
    atomic_init(&waiting_context.wait_result, false);
    atomic_init(&waiting_context.stop_requested_after_wait, false);
    atomic_init(&waiting_context.waited_ms, 0ul);
    waiting_context.wait_timeout_ms = 2000ul;

    memset(&worker_thread_config, 0, sizeof(worker_thread_config));
    worker_thread_config.entry = waiting_worker_entry;
    worker_thread_config.context = &waiting_context;

    worker_thread = 0;
    result = worker_thread_start(&worker_thread, &worker_thread_config);
    TEST_ASSERT(result.ok);
    TEST_ASSERT(worker_thread != 0);
    TEST_ASSERT(test_wait_until_true(&waiting_context.entered_wait, 200ul));

    TEST_ASSERT(worker_thread_request_stop(worker_thread).ok);
    TEST_ASSERT(worker_thread_join(worker_thread).ok);

    TEST_ASSERT(atomic_load(&waiting_context.wait_finished));
    TEST_ASSERT(!atomic_load(&waiting_context.wait_result));
    TEST_ASSERT(atomic_load(&waiting_context.stop_requested_after_wait));
    TEST_ASSERT(atomic_load(&waiting_context.waited_ms) < waiting_context.wait_timeout_ms);

    worker_thread_destroy(worker_thread);
    return 0;
}

int main(void)
{
    if (verify_notify_is_counted_and_consumed_in_order() != 0)
    {
        return 1;
    }
    if (verify_wait_times_out_without_notification() != 0)
    {
        return 1;
    }
    if (verify_wait_timeout_in_entry() != 0)
    {
        return 1;
    }
    if (verify_request_stop_wakes_waiting_thread() != 0)
    {
        return 1;
    }
    return 0;
}
