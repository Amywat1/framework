/**
 * @file    test_safety_posture.c
 * @brief   安全姿态边沿（经 alarm_event_bridge）单元测试
 */

#include "application/bridges/alarm_bridge.h"
#include "common/event_types.h"
#include "common/sw_error.h"
#include "common/time_util.h"
#include "domain/safety/alarm_registry/alarm_registry.h"
#include "domain/safety/model/alarm_types.h"
#include "runtime/event_bus/event_bus.h"
#include "wdf_test_spec.h"

static volatile int g_nominal_count;
static volatile int g_lockout_count;

static const alarm_def_t s_catalog[] = {
    {
     .code         = 201101U,
     .level        = ALARM_LEVEL_MAJOR,
     .clear        = ALARM_CLEAR_MANUAL_RESET,
     .reeval_group = ALARM_REEVAL_GROUP_NONE,
     .desc         = "侧刷过载",
     },
    {
     .code         = 201709U,
     .level        = ALARM_LEVEL_CRITICAL,
     .clear        = ALARM_CLEAR_AUTO_STATIC,
     .reeval_group = ALARM_REEVAL_GROUP_NONE,
     .desc         = "急停",
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

static void drain_registry(void)
{
    alarm_bridge_drain();
    TEST_ASSERT_EQUAL_INT(SW_OK, event_bus_drain());
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
    time_util_init();
    TEST_ASSERT_EQUAL_INT(SW_OK, event_bus_init());
    alarm_registry_init();
    (void)alarm_registry_load_catalog(s_catalog, 2U);
    TEST_ASSERT_EQUAL_INT(SW_OK, alarm_bridge_init());
    (void)event_subscribe(EVT_SAFETY_NOMINAL, on_nominal);
    (void)event_subscribe(EVT_SAFETY_LOCKOUT, on_lockout);

    (void)alarm_registry_trigger(201101U);
    drain_registry();

    TEST_ASSERT_EQUAL_INT(0, g_lockout_count);
}

static void test_critical_publishes_lockout(void)
{
    time_util_init();
    TEST_ASSERT_EQUAL_INT(SW_OK, event_bus_init());
    alarm_registry_init();
    (void)alarm_registry_load_catalog(s_catalog, 2U);
    TEST_ASSERT_EQUAL_INT(SW_OK, alarm_bridge_init());
    (void)event_subscribe(EVT_SAFETY_NOMINAL, on_nominal);
    (void)event_subscribe(EVT_SAFETY_LOCKOUT, on_lockout);

    (void)alarm_registry_trigger(201709U);
    drain_registry();

    TEST_ASSERT_EQUAL_INT(1, g_lockout_count);
}

int main(void)
{
    UNITY_BEGIN();

    WDF_RUN_TEST(test_major_no_lockout_event, "", "验证重大级无锁定事件");
    WDF_RUN_TEST(test_critical_publishes_lockout, "", "验证严重级发布锁定");

    return UNITY_END();
}
