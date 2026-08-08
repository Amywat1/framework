/**
 * @file    test_alarm_event_bridge.c
 * @brief   alarm_event_bridge 鍗曞厓娴嬭瘯
 */

#include "application/bridges/alarm_bridge.h"
#include "common/event_types.h"
#include "common/sw_error.h"
#include "common/time_util.h"
#include "domain/safety/alarm_registry/alarm_registry.h"
#include "domain/safety/model/alarm_types.h"
#include "runtime/event_bus/event_bus.h"
#include "wdf_test_spec.h"

#include <pthread.h>
#include <unistd.h>

static volatile int      g_trigger_count;
static volatile uint32_t g_last_code;

static const alarm_def_t s_catalog[] = {
    {
     .code         = 201101U,
     .level        = ALARM_LEVEL_MAJOR,
     .clear        = ALARM_CLEAR_MANUAL_RESET,
     .reeval_group = ALARM_REEVAL_GROUP_NONE,
     .desc         = "test",
     },
};

static void on_triggered(const event_t *evt)
{
    g_trigger_count++;
    g_last_code = evt->param;
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
    g_trigger_count = 0;
    g_last_code     = 0U;
}

void tearDown(void)
{
}

static void test_drain_publishes_triggered_event(void)
{
    pthread_t tid;

    time_util_init();
    TEST_ASSERT_EQUAL_INT(SW_OK, event_bus_init());
    TEST_ASSERT_EQUAL_INT(SW_OK, alarm_registry_init());
    (void)alarm_registry_load_catalog(s_catalog, 1U);
    TEST_ASSERT_EQUAL_INT(SW_OK, alarm_bridge_init());
    (void)event_subscribe(EVT_ALARM_TRIGGERED, on_triggered);

    tid = start_dispatch();
    usleep(10000);

    (void)alarm_registry_trigger(201101U);
    alarm_bridge_drain();
    usleep(50000);

    TEST_ASSERT_EQUAL_INT(1, g_trigger_count);
    TEST_ASSERT_EQUAL_UINT32(201101U, g_last_code);
    stop_dispatch(tid);
}

static void test_drain_empty_queue_no_event(void)
{
    pthread_t tid;

    time_util_init();
    TEST_ASSERT_EQUAL_INT(SW_OK, event_bus_init());
    TEST_ASSERT_EQUAL_INT(SW_OK, alarm_registry_init());
    (void)alarm_registry_load_catalog(s_catalog, 1U);
    (void)event_subscribe(EVT_ALARM_TRIGGERED, on_triggered);

    tid = start_dispatch();
    usleep(10000);

    alarm_bridge_drain();
    usleep(30000);

    TEST_ASSERT_EQUAL_INT(0, g_trigger_count);
    stop_dispatch(tid);
}

int main(void)
{
    UNITY_BEGIN();

    WDF_RUN_TEST(test_drain_publishes_triggered_event, "", "验证排空发布触发事件");
    WDF_RUN_TEST(test_drain_empty_queue_no_event, "", "验证排空空队列无事件");

    return UNITY_END();
}
