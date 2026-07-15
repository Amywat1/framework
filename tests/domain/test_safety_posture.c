/**
 * @file    test_safety_posture.c
 * @brief   safety_posture 单元测试
 */

#include "application/alarm_event_bridge.h"
#include "common/event_types.h"
#include "common/sw_error.h"
#include "common/time_util.h"
#include "domain/safety/alarm_registry/alarm_registry.h"
#include "domain/safety/model/alarm_types.h"
#include "domain/safety/safety_posture/safety_posture.h"
#include "runtime/event_bus/event_bus.h"
#include "unity.h"

#include <pthread.h>
#include <unistd.h>

static volatile int g_nominal_count;
static volatile int g_lockout_count;

static const alarm_def_t s_catalog[] = {
    {
     .code             = 201101U,
     .level            = ALARM_LEVEL_MAJOR,
     .response         = RESP_COMPLETE_THEN_ASSESS,
     .clear            = ALARM_CLEAR_MANUAL_RESET,
     .source_kind      = ALARM_SOURCE_LEVEL,
     .reeval_group     = ALARM_REEVAL_GROUP_NONE,
     .immediate_cutout = false,
     .desc             = "侧刷过载",
     },
    {
     .code             = 201709U,
     .level            = ALARM_LEVEL_CRITICAL,
     .response         = RESP_STOP_IMMEDIATELY,
     .clear            = ALARM_CLEAR_AUTO_STATIC,
     .source_kind      = ALARM_SOURCE_LEVEL,
     .reeval_group     = ALARM_REEVAL_GROUP_NONE,
     .immediate_cutout = false,
     .desc             = "急停",
     },
};

static void on_nominal(const event_t *evt)
{
    (void)evt;
    g_nominal_count++;
}

static void on_lockout(const event_t *evt)
{
    (void)evt;
    g_lockout_count++;
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

static void drain_registry(void)
{
    alarm_event_bridge_drain();
    usleep(50000);
}

void setUp(void)
{
    g_nominal_count = 0;
    g_lockout_count = 0;
}

void tearDown(void)
{
}

static void test_major_no_lockout_event(void)
{
    pthread_t tid;

    time_util_init();
    TEST_ASSERT_EQUAL_INT(SW_OK, event_bus_init());
    alarm_registry_init();
    (void)alarm_registry_load_catalog(s_catalog, 2U);
    TEST_ASSERT_EQUAL_INT(SW_OK, safety_posture_init());
    (void)event_subscribe(EVT_SAFETY_NOMINAL, on_nominal);
    (void)event_subscribe(EVT_SAFETY_LOCKOUT, on_lockout);

    tid = start_dispatch();
    usleep(10000);

    (void)alarm_registry_trigger(201101U);
    drain_registry();

    TEST_ASSERT_EQUAL_INT(0, g_lockout_count);
    stop_dispatch(tid);
}

static void test_critical_publishes_lockout(void)
{
    pthread_t tid;

    time_util_init();
    TEST_ASSERT_EQUAL_INT(SW_OK, event_bus_init());
    alarm_registry_init();
    (void)alarm_registry_load_catalog(s_catalog, 2U);
    TEST_ASSERT_EQUAL_INT(SW_OK, safety_posture_init());
    (void)event_subscribe(EVT_SAFETY_NOMINAL, on_nominal);
    (void)event_subscribe(EVT_SAFETY_LOCKOUT, on_lockout);

    tid = start_dispatch();
    usleep(10000);

    (void)alarm_registry_trigger(201709U);
    drain_registry();
    usleep(50000);

    TEST_ASSERT_EQUAL_INT(1, g_lockout_count);
    stop_dispatch(tid);
}

int main(void)
{
    UNITY_BEGIN();

    RUN_TEST(test_major_no_lockout_event);
    RUN_TEST(test_critical_publishes_lockout);

    return UNITY_END();
}
