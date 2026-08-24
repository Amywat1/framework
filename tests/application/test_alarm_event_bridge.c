/**
 * @file    test_alarm_event_bridge.c
 * @brief   alarm_bridge 入站绑定与变位入队单元测试
 */

#include "application/bridges/alarm_bridge.h"
#include "application/ports/inbound/safety/alarm_binding_port.h"
#include "common/event_types.h"
#include "common/sw_error.h"
#include "common/time_util.h"
#include "domain/safety/alarm_registry/alarm_registry.h"
#include "domain/safety/model/alarm_types.h"
#include "runtime/event_bus/event_bus.h"
#include "runtime/ports/port_registry.h"
#include "wdf_test_spec.h"

static volatile int      g_trigger_count;
static volatile uint32_t g_last_code;
static volatile int      g_cleared_count;
static volatile int      g_lockout_count;
static volatile int      g_nominal_count;

static const alarm_def_t s_catalog[] = {
    {
     .code         = 201101U,
     .level        = ALARM_LEVEL_MAJOR,
     .clear        = ALARM_CLEAR_MANUAL_RESET,
     .reeval_group = ALARM_REEVAL_GROUP_NONE,
     .desc         = "test",
     },
    {
     .code         = 201709U,
     .level        = ALARM_LEVEL_CRITICAL,
     .clear        = ALARM_CLEAR_AUTO_STATIC,
     .reeval_group = ALARM_REEVAL_GROUP_NONE,
     .desc         = "test critical",
     },
    {
     .code         = 201809U,
     .level        = ALARM_LEVEL_CRITICAL,
     .clear        = ALARM_CLEAR_AUTO_STATIC,
     .reeval_group = ALARM_REEVAL_GROUP_NONE,
     .desc         = "test critical 2",
     },
    {
     .code         = 901001U,
     .level        = ALARM_LEVEL_MINOR,
     .clear        = ALARM_CLEAR_AUTO_STATIC,
     .reeval_group = ALARM_REEVAL_GROUP_NONE,
     .desc         = "test minor",
     },
};

#define CATALOG_COUNT (sizeof(s_catalog) / sizeof(s_catalog[0]))

static void on_triggered(const event_t *evt)
{
    g_trigger_count++;
    g_last_code = evt->param;
}

static void on_cleared(const event_t *evt)
{
    (void)evt;
    g_cleared_count++;
}

static void on_lockout(const event_t *evt)
{
    (void)evt;
    g_lockout_count++;
}

static void on_nominal(const event_t *evt)
{
    (void)evt;
    g_nominal_count++;
}

void setUp(void)
{
    g_trigger_count = 0;
    g_last_code     = 0U;
    g_cleared_count = 0;
    g_lockout_count = 0;
    g_nominal_count = 0;
}

void tearDown(void)
{
}

static const alarm_binding_ops_t *bind_ops(void)
{
    const alarm_binding_ops_t *ops;

    port_registry_infra_reset();
    TEST_ASSERT_EQUAL_INT(SW_OK, alarm_bridge_bind());
    ops = alarm_binding_get_ops();
    TEST_ASSERT_NOT_NULL(ops);
    TEST_ASSERT_NOT_NULL(ops->trigger);
    TEST_ASSERT_NOT_NULL(ops->load_catalog);
    return ops;
}

static void test_trigger_publishes_before_return(void)
{
    time_util_init();
    TEST_ASSERT_EQUAL_INT(SW_OK, event_bus_init());
    TEST_ASSERT_EQUAL_INT(SW_OK, alarm_registry_init());
    (void)alarm_registry_load_catalog(s_catalog, (unsigned)CATALOG_COUNT);
    TEST_ASSERT_EQUAL_INT(SW_OK, alarm_bridge_init());
    (void)event_subscribe(EVT_ALARM_TRIGGERED, on_triggered);

    (void)alarm_registry_trigger(201101U);
    TEST_ASSERT_EQUAL_INT(SW_OK, event_bus_drain());

    TEST_ASSERT_EQUAL_INT(1, g_trigger_count);
    TEST_ASSERT_EQUAL_UINT32(201101U, g_last_code);
}

static void test_no_event_when_idle(void)
{
    time_util_init();
    TEST_ASSERT_EQUAL_INT(SW_OK, event_bus_init());
    TEST_ASSERT_EQUAL_INT(SW_OK, alarm_registry_init());
    (void)alarm_registry_load_catalog(s_catalog, (unsigned)CATALOG_COUNT);
    (void)event_subscribe(EVT_ALARM_TRIGGERED, on_triggered);

    TEST_ASSERT_EQUAL_INT(SW_OK, event_bus_drain());
    TEST_ASSERT_EQUAL_INT(0, g_trigger_count);
}

static void test_binding_trigger_lockout_publishes_before_return(void)
{
    const alarm_binding_ops_t *ops;

    time_util_init();
    TEST_ASSERT_EQUAL_INT(SW_OK, event_bus_init());
    TEST_ASSERT_EQUAL_INT(SW_OK, alarm_registry_init());
    ops = bind_ops();
    TEST_ASSERT_EQUAL_INT(SW_OK, ops->load_catalog(s_catalog, (unsigned)CATALOG_COUNT));
    TEST_ASSERT_EQUAL_INT(SW_OK, event_subscribe(EVT_ALARM_TRIGGERED, on_triggered));
    TEST_ASSERT_EQUAL_INT(SW_OK, event_subscribe(EVT_SAFETY_LOCKOUT, on_lockout));

    TEST_ASSERT_EQUAL_INT(SW_OK, ops->trigger(201709U));
    TEST_ASSERT_EQUAL_INT(SAFETY_POSTURE_LOCKOUT, alarm_registry_safety_posture());
    TEST_ASSERT_EQUAL_INT(SW_OK, event_bus_drain());

    TEST_ASSERT_EQUAL_INT(1, g_trigger_count);
    TEST_ASSERT_EQUAL_INT(1, g_lockout_count);
}

static void test_binding_trigger_minor_publishes_before_return(void)
{
    const alarm_binding_ops_t *ops;

    time_util_init();
    TEST_ASSERT_EQUAL_INT(SW_OK, event_bus_init());
    TEST_ASSERT_EQUAL_INT(SW_OK, alarm_registry_init());
    ops = bind_ops();
    TEST_ASSERT_EQUAL_INT(SW_OK, ops->load_catalog(s_catalog, (unsigned)CATALOG_COUNT));
    TEST_ASSERT_EQUAL_INT(SW_OK, event_subscribe(EVT_ALARM_TRIGGERED, on_triggered));

    TEST_ASSERT_EQUAL_INT(SW_OK, ops->trigger(901001U));
    TEST_ASSERT_EQUAL_INT(SW_OK, event_bus_drain());
    TEST_ASSERT_EQUAL_INT(1, g_trigger_count);
    TEST_ASSERT_EQUAL_INT(SAFETY_POSTURE_NOMINAL, alarm_registry_safety_posture());
}

static void test_binding_second_critical_does_not_repeat_lockout(void)
{
    const alarm_binding_ops_t *ops;

    time_util_init();
    TEST_ASSERT_EQUAL_INT(SW_OK, event_bus_init());
    TEST_ASSERT_EQUAL_INT(SW_OK, alarm_registry_init());
    ops = bind_ops();
    TEST_ASSERT_EQUAL_INT(SW_OK, ops->load_catalog(s_catalog, (unsigned)CATALOG_COUNT));
    TEST_ASSERT_EQUAL_INT(SW_OK, event_subscribe(EVT_ALARM_TRIGGERED, on_triggered));
    TEST_ASSERT_EQUAL_INT(SW_OK, event_subscribe(EVT_SAFETY_LOCKOUT, on_lockout));
    TEST_ASSERT_EQUAL_INT(SW_OK, event_subscribe(EVT_SAFETY_NOMINAL, on_nominal));
    TEST_ASSERT_EQUAL_INT(SW_OK, event_subscribe(EVT_ALARM_CLEARED, on_cleared));

    TEST_ASSERT_EQUAL_INT(SW_OK, ops->trigger(201709U));
    TEST_ASSERT_EQUAL_INT(SW_OK, event_bus_drain());
    TEST_ASSERT_EQUAL_INT(1, g_lockout_count);

    TEST_ASSERT_EQUAL_INT(SW_OK, ops->trigger(201809U));
    TEST_ASSERT_EQUAL_INT(SW_OK, event_bus_drain());
    TEST_ASSERT_EQUAL_INT(2, g_trigger_count);
    TEST_ASSERT_EQUAL_INT(1, g_lockout_count);
    TEST_ASSERT_EQUAL_INT(0, g_nominal_count);
}

int main(void)
{
    UNITY_BEGIN();

    WDF_RUN_TEST(test_trigger_publishes_before_return, "", "验证 trigger 返回前已入队 TRIGGERED");
    WDF_RUN_TEST(test_no_event_when_idle, "", "验证空闲时排空无事件");
    WDF_RUN_TEST(test_binding_trigger_lockout_publishes_before_return,
                 "ALRM-22",
                 "验证 binding trigger CRITICAL 返回前已入队 LOCKOUT");
    WDF_RUN_TEST(test_binding_trigger_minor_publishes_before_return, "ALRM-22", "验证 MINOR 返回前已入队 TRIGGERED");
    WDF_RUN_TEST(test_binding_second_critical_does_not_repeat_lockout, "ALRM-22", "验证第二条 CRITICAL 不重复 LOCKOUT");

    return UNITY_END();
}
