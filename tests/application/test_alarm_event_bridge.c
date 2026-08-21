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
    time_util_init();
    TEST_ASSERT_EQUAL_INT(SW_OK, event_bus_init());
    TEST_ASSERT_EQUAL_INT(SW_OK, alarm_registry_init());
    (void)alarm_registry_load_catalog(s_catalog, 1U);
    TEST_ASSERT_EQUAL_INT(SW_OK, alarm_bridge_init());
    (void)event_subscribe(EVT_ALARM_TRIGGERED, on_triggered);

    (void)alarm_registry_trigger(201101U);
    alarm_bridge_drain();
    TEST_ASSERT_EQUAL_INT(SW_OK, event_bus_drain());

    TEST_ASSERT_EQUAL_INT(1, g_trigger_count);
    TEST_ASSERT_EQUAL_UINT32(201101U, g_last_code);
}

static void test_drain_empty_queue_no_event(void)
{
    time_util_init();
    TEST_ASSERT_EQUAL_INT(SW_OK, event_bus_init());
    TEST_ASSERT_EQUAL_INT(SW_OK, alarm_registry_init());
    (void)alarm_registry_load_catalog(s_catalog, 1U);
    (void)event_subscribe(EVT_ALARM_TRIGGERED, on_triggered);

    alarm_bridge_drain();
    TEST_ASSERT_EQUAL_INT(SW_OK, event_bus_drain());

    TEST_ASSERT_EQUAL_INT(0, g_trigger_count);
}

int main(void)
{
    UNITY_BEGIN();

    WDF_RUN_TEST(test_drain_publishes_triggered_event, "", "验证排空发布触发事件");
    WDF_RUN_TEST(test_drain_empty_queue_no_event, "", "验证排空空队列无事件");

    return UNITY_END();
}
