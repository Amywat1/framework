/**
 * @file    test_hal_io_manager.c
 * @brief   通用 HAL I/O 单所有者事务管理器测试。
 */

#include "adapters/outbound/hal/components/io_manager/hal_io_manager.h"
#include "common/time_util.h"
#include "wdf_test_spec.h"

#include <errno.h>
#include <pthread.h>
#include <stdbool.h>
#include <string.h>
#include <unistd.h>

#define TEST_WAIT_TIMEOUT_MS 1000U

typedef struct {
    pthread_mutex_t mutex;
    pthread_cond_t  cond;
    bool            gate_open;
    bool            gate_entered;
    int             order[8];
    unsigned        order_count;
    unsigned        tick_count;
    pthread_t       worker;
    bool            worker_seen;
    bool            worker_consistent;
} manager_test_ctx_t;

typedef struct {
    int                       request;
    hal_io_manager_priority_t priority;
    uint32_t                  timeout_ms;
    int                       response;
    sw_err_t                  result;
} caller_ctx_t;

static manager_test_ctx_t s_test_ctx = {
    .mutex = PTHREAD_MUTEX_INITIALIZER,
    .cond  = PTHREAD_COND_INITIALIZER,
};

static void record_worker_locked(void)
{
    pthread_t current = pthread_self();

    if (!s_test_ctx.worker_seen) {
        s_test_ctx.worker            = current;
        s_test_ctx.worker_seen       = true;
        s_test_ctx.worker_consistent = true;
    } else if (pthread_equal(s_test_ctx.worker, current) == 0) {
        s_test_ctx.worker_consistent = false;
    }
}

static void test_tick(void *ctx)
{
    (void)ctx;
    (void)pthread_mutex_lock(&s_test_ctx.mutex);
    record_worker_locked();
    s_test_ctx.tick_count++;
    (void)pthread_cond_broadcast(&s_test_ctx.cond);
    (void)pthread_mutex_unlock(&s_test_ctx.mutex);
}

static sw_err_t test_execute(void *ctx, const void *request, size_t request_size, void *response, size_t response_size)
{
    const int      *value  = (const int *)request;
    int            *result = (int *)response;
    struct timespec deadline;
    int             wait_ret = 0;

    (void)ctx;
    if ((value == NULL) || (result == NULL) || (request_size != sizeof(*value)) || (response_size != sizeof(*result))) {
        return SW_ERR_PARAM;
    }

    (void)pthread_mutex_lock(&s_test_ctx.mutex);
    record_worker_locked();
    if (s_test_ctx.order_count < (sizeof(s_test_ctx.order) / sizeof(s_test_ctx.order[0]))) {
        s_test_ctx.order[s_test_ctx.order_count++] = *value;
    }
    if (*value == 0) {
        s_test_ctx.gate_entered = true;
        (void)pthread_cond_broadcast(&s_test_ctx.cond);
        time_util_fill_deadline(TEST_WAIT_TIMEOUT_MS, &deadline);
        while (!s_test_ctx.gate_open && (wait_ret != ETIMEDOUT)) {
            wait_ret = pthread_cond_timedwait(&s_test_ctx.cond, &s_test_ctx.mutex, &deadline);
        }
        if (!s_test_ctx.gate_open) {
            (void)pthread_mutex_unlock(&s_test_ctx.mutex);
            return SW_ERR_TIMEOUT;
        }
    }
    (void)pthread_mutex_unlock(&s_test_ctx.mutex);

    if (*value < 0) {
        usleep(100U * 1000U);
    }
    *result = *value * 10;
    return SW_OK;
}

static hal_io_manager_cfg_t make_cfg(void)
{
    hal_io_manager_cfg_t cfg = {
        .name           = "manager_test",
        .tick_period_ms = 1000U,
        .tick           = test_tick,
        .tick_ctx       = NULL,
        .execute        = test_execute,
        .execute_ctx    = NULL,
    };

    return cfg;
}

static void wait_for_gate_entry(void)
{
    struct timespec deadline;
    int             wait_ret = 0;
    bool            gate_entered;

    time_util_fill_deadline(TEST_WAIT_TIMEOUT_MS, &deadline);
    (void)pthread_mutex_lock(&s_test_ctx.mutex);
    while (!s_test_ctx.gate_entered && (wait_ret != ETIMEDOUT)) {
        wait_ret = pthread_cond_timedwait(&s_test_ctx.cond, &s_test_ctx.mutex, &deadline);
    }
    gate_entered = s_test_ctx.gate_entered;
    (void)pthread_mutex_unlock(&s_test_ctx.mutex);
    TEST_ASSERT_TRUE(gate_entered);
}

static void open_gate(void)
{
    (void)pthread_mutex_lock(&s_test_ctx.mutex);
    s_test_ctx.gate_open = true;
    (void)pthread_cond_broadcast(&s_test_ctx.cond);
    (void)pthread_mutex_unlock(&s_test_ctx.mutex);
}

static void *caller_thread(void *arg)
{
    caller_ctx_t *caller = (caller_ctx_t *)arg;

    caller->result = hal_io_manager_call(caller->priority,
                                         caller->timeout_ms,
                                         &caller->request,
                                         sizeof(caller->request),
                                         &caller->response,
                                         sizeof(caller->response));
    return NULL;
}

void setUp(void)
{
    TEST_ASSERT_EQUAL_INT(SW_OK, hal_io_manager_reset_for_test());
    (void)pthread_mutex_lock(&s_test_ctx.mutex);
    s_test_ctx.gate_open         = true;
    s_test_ctx.gate_entered      = false;
    s_test_ctx.order_count       = 0U;
    s_test_ctx.tick_count        = 0U;
    s_test_ctx.worker_seen       = false;
    s_test_ctx.worker_consistent = true;
    memset(s_test_ctx.order, 0, sizeof(s_test_ctx.order));
    (void)pthread_mutex_unlock(&s_test_ctx.mutex);
}

void tearDown(void)
{
    open_gate();
    TEST_ASSERT_EQUAL_INT(SW_OK, hal_io_manager_reset_for_test());
}

static void test_lifecycle_and_response_copy(void)
{
    hal_io_manager_cfg_t cfg      = make_cfg();
    int                  request  = 7;
    int                  response = 0;

    TEST_ASSERT_EQUAL_INT(
        SW_ERR_NOT_INIT,
        hal_io_manager_call(
            HAL_IO_MANAGER_PRIORITY_NORMAL, 100U, &request, sizeof(request), &response, sizeof(response)));
    TEST_ASSERT_EQUAL_INT(SW_OK, hal_io_manager_init(&cfg));
    TEST_ASSERT_EQUAL_INT(
        SW_ERR_STATE,
        hal_io_manager_call(
            HAL_IO_MANAGER_PRIORITY_NORMAL, 100U, &request, sizeof(request), &response, sizeof(response)));
    TEST_ASSERT_EQUAL_INT(SW_OK, hal_io_manager_start());
    TEST_ASSERT_EQUAL_INT(
        SW_OK,
        hal_io_manager_call(
            HAL_IO_MANAGER_PRIORITY_NORMAL, 100U, &request, sizeof(request), &response, sizeof(response)));
    TEST_ASSERT_EQUAL_INT(70, response);
    TEST_ASSERT_TRUE(s_test_ctx.worker_seen);
    TEST_ASSERT_TRUE(s_test_ctx.worker_consistent);
}

static void test_caller_timeout_is_bounded(void)
{
    hal_io_manager_cfg_t cfg      = make_cfg();
    int                  request  = -1;
    int                  response = 0;
    uint64_t             start_ms;

    TEST_ASSERT_EQUAL_INT(SW_OK, hal_io_manager_init(&cfg));
    TEST_ASSERT_EQUAL_INT(SW_OK, hal_io_manager_start());
    start_ms = time_util_get_ms();
    TEST_ASSERT_EQUAL_INT(
        SW_ERR_TIMEOUT,
        hal_io_manager_call(
            HAL_IO_MANAGER_PRIORITY_NORMAL, 20U, &request, sizeof(request), &response, sizeof(response)));
    TEST_ASSERT_LESS_THAN_UINT32(100U, time_elapsed_ms(start_ms, time_util_get_ms()));
}

static void test_higher_priority_queued_request_runs_first(void)
{
    hal_io_manager_cfg_t cfg     = make_cfg();
    caller_ctx_t         blocker = {0, HAL_IO_MANAGER_PRIORITY_NORMAL, 800U, 0, SW_ERR_STATE};
    caller_ctx_t         low     = {1, HAL_IO_MANAGER_PRIORITY_LOW, 800U, 0, SW_ERR_STATE};
    caller_ctx_t         high    = {2, HAL_IO_MANAGER_PRIORITY_HIGH, 800U, 0, SW_ERR_STATE};
    pthread_t            blocker_thread;
    pthread_t            low_thread;
    pthread_t            high_thread;

    s_test_ctx.gate_open = false;
    TEST_ASSERT_EQUAL_INT(SW_OK, hal_io_manager_init(&cfg));
    TEST_ASSERT_EQUAL_INT(SW_OK, hal_io_manager_start());
    TEST_ASSERT_EQUAL_INT(0, pthread_create(&blocker_thread, NULL, caller_thread, &blocker));
    wait_for_gate_entry();
    TEST_ASSERT_EQUAL_INT(0, pthread_create(&low_thread, NULL, caller_thread, &low));
    TEST_ASSERT_EQUAL_INT(0, pthread_create(&high_thread, NULL, caller_thread, &high));
    usleep(20U * 1000U);
    open_gate();

    (void)pthread_join(blocker_thread, NULL);
    (void)pthread_join(low_thread, NULL);
    (void)pthread_join(high_thread, NULL);
    TEST_ASSERT_EQUAL_INT(SW_OK, blocker.result);
    TEST_ASSERT_EQUAL_INT(SW_OK, low.result);
    TEST_ASSERT_EQUAL_INT(SW_OK, high.result);
    TEST_ASSERT_EQUAL_UINT(3U, s_test_ctx.order_count);
    TEST_ASSERT_EQUAL_INT(0, s_test_ctx.order[0]);
    TEST_ASSERT_EQUAL_INT(2, s_test_ctx.order[1]);
    TEST_ASSERT_EQUAL_INT(1, s_test_ctx.order[2]);
}

static void test_expired_queued_request_does_not_reach_backend(void)
{
    hal_io_manager_cfg_t cfg     = make_cfg();
    caller_ctx_t         blocker = {0, HAL_IO_MANAGER_PRIORITY_NORMAL, 800U, 0, SW_ERR_STATE};
    caller_ctx_t         expired = {3, HAL_IO_MANAGER_PRIORITY_HIGH, 20U, 0, SW_ERR_STATE};
    pthread_t            blocker_thread;
    pthread_t            expired_thread;

    s_test_ctx.gate_open = false;
    TEST_ASSERT_EQUAL_INT(SW_OK, hal_io_manager_init(&cfg));
    TEST_ASSERT_EQUAL_INT(SW_OK, hal_io_manager_start());
    TEST_ASSERT_EQUAL_INT(0, pthread_create(&blocker_thread, NULL, caller_thread, &blocker));
    wait_for_gate_entry();
    TEST_ASSERT_EQUAL_INT(0, pthread_create(&expired_thread, NULL, caller_thread, &expired));
    (void)pthread_join(expired_thread, NULL);
    TEST_ASSERT_EQUAL_INT(SW_ERR_TIMEOUT, expired.result);
    open_gate();
    (void)pthread_join(blocker_thread, NULL);
    usleep(20U * 1000U);

    TEST_ASSERT_EQUAL_INT(SW_OK, blocker.result);
    TEST_ASSERT_EQUAL_UINT(1U, s_test_ctx.order_count);
    TEST_ASSERT_EQUAL_INT(0, s_test_ctx.order[0]);
}

int main(void)
{
    UNITY_BEGIN();
    WDF_RUN_TEST(test_lifecycle_and_response_copy, "", "验证manager生命周期与响应复制");
    WDF_RUN_TEST(test_caller_timeout_is_bounded, "", "验证manager调用方等待有界");
    WDF_RUN_TEST(test_higher_priority_queued_request_runs_first, "", "验证manager优先执行高优先级排队请求");
    WDF_RUN_TEST(test_expired_queued_request_does_not_reach_backend, "", "验证manager丢弃已过期排队请求");
    return UNITY_END();
}
