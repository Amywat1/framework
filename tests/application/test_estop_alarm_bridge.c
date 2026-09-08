/**
 * @file    test_estop_alarm_bridge.c
 * @brief   急停确认边沿投影为报警的单元测试
 */

#include "application/bridges/estop_alarm_bridge.h"
#include "common/event_types.h"
#include "common/sw_error.h"
#include "common/time_util.h"
#include "domain/safety/alarm_registry/alarm_registry.h"
#include "domain/safety/model/alarm_types.h"
#include "runtime/event_bus/event_bus.h"
#include "wdf_test_spec.h"

#define TEST_ESTOP_CODE 201709U

static volatile int s_lockout_count;
static volatile int s_nominal_count;

static const alarm_def_t s_catalog[] = {
    {
     .code         = TEST_ESTOP_CODE,
     .level        = ALARM_LEVEL_CRITICAL,
     .clear        = ALARM_CLEAR_AUTO_STATIC,
     .reeval_group = ALARM_REEVAL_GROUP_NONE,
     .desc         = "test estop",
     },
};

static void on_lockout(const event_t *evt)
{
    (void)evt;
    s_lockout_count++;
}

static void on_nominal(const event_t *evt)
{
    (void)evt;
    s_nominal_count++;
}

static void publish_and_wait(event_type_t type)
{
    TEST_ASSERT_EQUAL_INT(SW_OK, event_publish(type, 0U));
    TEST_ASSERT_EQUAL_INT(SW_OK, event_bus_drain());
}

void setUp(void)
{
    s_lockout_count = 0;
    s_nominal_count = 0;
    time_util_init();
    TEST_ASSERT_EQUAL_INT(SW_OK, event_bus_init());
    TEST_ASSERT_EQUAL_INT(SW_OK, alarm_registry_init());
    TEST_ASSERT_EQUAL_INT(SW_OK, alarm_registry_load_catalog(s_catalog, 1U));
    TEST_ASSERT_EQUAL_INT(SW_OK, event_subscribe(EVT_SAFETY_LOCKOUT, on_lockout));
    TEST_ASSERT_EQUAL_INT(SW_OK, event_subscribe(EVT_SAFETY_NOMINAL, on_nominal));
}

void tearDown(void)
{
}

static void test_rejects_invalid_code(void)
{
    TEST_ASSERT_EQUAL_INT(SW_ERR_PARAM, estop_alarm_bridge_init(ALARM_CODE_NONE));
}

static void test_hw_estop_on_triggers_lockout(void)
{
    TEST_ASSERT_EQUAL_INT(SW_OK, estop_alarm_bridge_init(TEST_ESTOP_CODE));

    publish_and_wait(EVT_HW_ESTOP_ON);

    TEST_ASSERT_TRUE(alarm_registry_is_active(TEST_ESTOP_CODE));
    TEST_ASSERT_EQUAL_INT(SAFETY_POSTURE_LOCKOUT, alarm_registry_safety_posture());
    TEST_ASSERT_EQUAL_INT(1, s_lockout_count);
}

static void test_hw_estop_off_clears_to_nominal(void)
{
    TEST_ASSERT_EQUAL_INT(SW_OK, estop_alarm_bridge_init(TEST_ESTOP_CODE));

    publish_and_wait(EVT_HW_ESTOP_ON);
    publish_and_wait(EVT_HW_ESTOP_OFF);

    TEST_ASSERT_FALSE(alarm_registry_is_active(TEST_ESTOP_CODE));
    TEST_ASSERT_EQUAL_INT(SAFETY_POSTURE_NOMINAL, alarm_registry_safety_posture());
    TEST_ASSERT_EQUAL_INT(1, s_lockout_count);
    TEST_ASSERT_EQUAL_INT(1, s_nominal_count);
}

int main(void)
{
    UNITY_BEGIN();

    WDF_RUN_TEST(test_rejects_invalid_code, "", "验证非法报警码拒绝初始化");
    WDF_RUN_TEST(test_hw_estop_on_triggers_lockout, "", "验证确认按下投影急停报警并进入 LOCKOUT");
    WDF_RUN_TEST(test_hw_estop_off_clears_to_nominal, "", "验证确认松开清除急停报警并回到 NOMINAL");

    return UNITY_END();
}
