/**
 * @file    test_cloud_point_watcher.c
 * @brief   cloud_point_watcher ON_CHANGE 变更检测单元测试
 */

#include "cloud/cloud_point.h"
#include "cloud/cloud_point_watcher.h"
#include "common/event_types.h"
#include "common/sw_error.h"
#include "runtime/event_bus/event_bus.h"
#include "unity.h"

#include <pthread.h>
#include <string.h>
#include <unistd.h>

static int32_t           s_value;
static volatile int      g_dirty_count;
static volatile uint32_t g_dirty_index;

static sw_err_t get_value(point_value_t *out)
{
    out->i = s_value;
    return SW_OK;
}

static void on_dirty(const event_t *evt)
{
    g_dirty_count++;
    g_dirty_index = evt->param;
}

static cloud_point_entry_t make_on_change_entry(void)
{
    cloud_point_entry_t entry;

    memset(&entry, 0, sizeof(entry));
    entry.base.id       = "speed";
    entry.base.type     = POINT_TYPE_INT;
    entry.base.get      = get_value;
    entry.access        = CLOUD_POINT_ACCESS_RO;
    entry.semantic      = CLOUD_POINT_SEM_TELEMETRY;
    entry.report_policy = CLOUD_REPORT_ON_CHANGE;
    return entry;
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

static void reset_flags(void)
{
    s_value       = 10;
    g_dirty_count = 0;
    g_dirty_index = 999U;
}

void setUp(void)
{
}

void tearDown(void)
{
}

static void test_poll_without_change_no_event(void)
{
    const cloud_point_entry_t entries[] = {make_on_change_entry()};
    pthread_t                 tid;

    reset_flags();
    TEST_ASSERT_EQUAL_INT(SW_OK, event_bus_init());
    TEST_ASSERT_EQUAL_INT(SW_OK, event_subscribe(EVT_CLOUD_POINT_DIRTY, on_dirty));
    tid = start_dispatch();
    TEST_ASSERT_EQUAL_INT(SW_OK, cloud_point_watcher_init(entries, 1U));

    cloud_point_watcher_poll();
    usleep(30000);

    TEST_ASSERT_EQUAL_INT(0, g_dirty_count);
    stop_dispatch(tid);
}

static void test_poll_after_change_publishes_dirty(void)
{
    const cloud_point_entry_t entries[] = {make_on_change_entry()};
    pthread_t                 tid;

    reset_flags();
    TEST_ASSERT_EQUAL_INT(SW_OK, event_bus_init());
    TEST_ASSERT_EQUAL_INT(SW_OK, event_subscribe(EVT_CLOUD_POINT_DIRTY, on_dirty));
    tid = start_dispatch();
    TEST_ASSERT_EQUAL_INT(SW_OK, cloud_point_watcher_init(entries, 1U));

    s_value = 20;
    cloud_point_watcher_poll();
    usleep(30000);

    TEST_ASSERT_EQUAL_INT(1, g_dirty_count);
    TEST_ASSERT_EQUAL_UINT32(0U, g_dirty_index);
    stop_dispatch(tid);
}

static void test_init_rejects_invalid_args(void)
{
    const cloud_point_entry_t entries[] = {make_on_change_entry()};

    TEST_ASSERT_EQUAL_INT(SW_ERR_PARAM, cloud_point_watcher_init(NULL, 1U));
    TEST_ASSERT_EQUAL_INT(SW_ERR_PARAM, cloud_point_watcher_init(entries, 0U));
}

int main(void)
{
    UNITY_BEGIN();

    RUN_TEST(test_poll_without_change_no_event);
    RUN_TEST(test_poll_after_change_publishes_dirty);
    RUN_TEST(test_init_rejects_invalid_args);

    return UNITY_END();
}
