/**
 * @file    test_alarm_reeval_bridge.c
 * @brief   alarm_reeval_bridge 鍗曞厓娴嬭瘯
 */

#include "application/bridges/alarm_bridge.h"
#include "common/sw_error.h"
#include "common/time_util.h"
#include "domain/mechanism/model/actuator_events.h"
#include "domain/safety/alarm_registry/alarm_registry.h"
#include "domain/safety/model/alarm_types.h"
#include "domain/wash/wash_events.h"
#include "runtime/event_bus/event_bus.h"
#include "wdf_test_spec.h"

enum {
    TEST_ACTUATOR_GANTRY = 11,
    TEST_CHECKPOINT_EXIT = 21,
    TEST_GROUP_GANTRY    = 101,
    TEST_GROUP_EXIT      = 102,
};

static const alarm_def_t s_catalog[] = {
    {
     .code         = 201105U,
     .level        = ALARM_LEVEL_MAJOR,
     .clear        = ALARM_CLEAR_ON_MOTION,
     .reeval_group = TEST_GROUP_GANTRY,
     .desc         = "gantry timeout",
     },
    {
     .code         = 201205U,
     .level        = ALARM_LEVEL_MAJOR,
     .clear        = ALARM_CLEAR_ON_MOTION,
     .reeval_group = TEST_GROUP_EXIT,
     .desc         = "exit timeout",
     },
};

static const alarm_reeval_binding_t s_bindings[] = {
    {ALARM_REEVAL_TRIGGER_ACTUATOR_COMPLETED, TEST_ACTUATOR_GANTRY, TEST_GROUP_GANTRY},
    {ALARM_REEVAL_TRIGGER_WASH_CHECKPOINT,    TEST_CHECKPOINT_EXIT, TEST_GROUP_EXIT  },
};

void setUp(void)
{
    time_util_init();
    TEST_ASSERT_EQUAL_INT(SW_OK, event_bus_init());
    TEST_ASSERT_EQUAL_INT(SW_OK, alarm_registry_init());
    TEST_ASSERT_EQUAL_INT(SW_OK, alarm_registry_load_catalog(s_catalog, 2U));
}

void tearDown(void)
{
}

static void test_init_rejects_invalid_binding_table(void)
{
    const alarm_reeval_binding_t zero_trigger[] = {
        {ALARM_REEVAL_TRIGGER_ACTUATOR_COMPLETED, 0U, TEST_GROUP_GANTRY},
    };
    const alarm_reeval_binding_t no_group[] = {
        {ALARM_REEVAL_TRIGGER_ACTUATOR_COMPLETED, TEST_ACTUATOR_GANTRY, ALARM_REEVAL_GROUP_NONE},
    };
    const alarm_reeval_binding_t duplicate[] = {
        {ALARM_REEVAL_TRIGGER_ACTUATOR_COMPLETED, TEST_ACTUATOR_GANTRY, TEST_GROUP_GANTRY},
        {ALARM_REEVAL_TRIGGER_ACTUATOR_COMPLETED, TEST_ACTUATOR_GANTRY, TEST_GROUP_EXIT  },
    };

    TEST_ASSERT_EQUAL_INT(SW_ERR_PARAM, alarm_bridge_reeval_init(NULL, 1U));
    TEST_ASSERT_EQUAL_INT(SW_ERR_PARAM, alarm_bridge_reeval_init(zero_trigger, 1U));
    TEST_ASSERT_EQUAL_INT(SW_ERR_PARAM, alarm_bridge_reeval_init(no_group, 1U));
    TEST_ASSERT_EQUAL_INT(SW_ERR_PARAM, alarm_bridge_reeval_init(duplicate, 2U));
}

static void test_actuator_event_reevaluates_bound_group_only(void)
{
    TEST_ASSERT_EQUAL_INT(SW_OK, alarm_bridge_reeval_init(s_bindings, 2U));
    TEST_ASSERT_EQUAL_INT(SW_OK, alarm_registry_trigger(201105U));
    TEST_ASSERT_EQUAL_INT(SW_OK, alarm_registry_trigger(201205U));
    TEST_ASSERT_TRUE(alarm_registry_is_active(201105U));
    TEST_ASSERT_TRUE(alarm_registry_is_active(201205U));

    /* 仅龙门侧在动作中判为正常；条件仍成立的分组不得被运动结束误清 */
    TEST_ASSERT_EQUAL_INT(SW_OK, alarm_registry_clear(201105U));

    actuator_publish_motion_completed(TEST_ACTUATOR_GANTRY);
    TEST_ASSERT_EQUAL_INT(SW_OK, event_bus_drain());

    TEST_ASSERT_FALSE(alarm_registry_is_active(201105U));
    TEST_ASSERT_TRUE(alarm_registry_is_active(201205U));
}

static void test_checkpoint_event_reevaluates_bound_group(void)
{
    TEST_ASSERT_EQUAL_INT(SW_OK, alarm_bridge_reeval_init(s_bindings, 2U));
    TEST_ASSERT_EQUAL_INT(SW_OK, alarm_registry_trigger(201205U));
    TEST_ASSERT_EQUAL_INT(SW_OK, alarm_registry_clear(201205U));

    wash_publish_checkpoint_reached(TEST_CHECKPOINT_EXIT);
    TEST_ASSERT_EQUAL_INT(SW_OK, event_bus_drain());

    TEST_ASSERT_FALSE(alarm_registry_is_active(201205U));
}

static void test_motion_complete_keeps_active_condition(void)
{
    TEST_ASSERT_EQUAL_INT(SW_OK, alarm_bridge_reeval_init(s_bindings, 2U));
    TEST_ASSERT_EQUAL_INT(SW_OK, alarm_registry_trigger(201105U));

    actuator_publish_motion_completed(TEST_ACTUATOR_GANTRY);
    TEST_ASSERT_EQUAL_INT(SW_OK, event_bus_drain());

    TEST_ASSERT_TRUE(alarm_registry_is_active(201105U));
}

static void test_unbound_trigger_is_noop(void)
{
    TEST_ASSERT_EQUAL_INT(SW_OK, alarm_bridge_reeval_init(s_bindings, 2U));
    TEST_ASSERT_EQUAL_INT(SW_OK, alarm_registry_trigger(201105U));

    TEST_ASSERT_EQUAL_INT(SW_OK, alarm_bridge_reeval_handle(ALARM_REEVAL_TRIGGER_ACTUATOR_COMPLETED, 99U));
    TEST_ASSERT_TRUE(alarm_registry_is_active(201105U));
}

int main(void)
{
    UNITY_BEGIN();

    WDF_RUN_TEST(test_init_rejects_invalid_binding_table, "", "验证初始化拒绝无效绑定表");
    WDF_RUN_TEST(test_actuator_event_reevaluates_bound_group_only, "", "验证执行器事件仅重新评估绑定的报警组");
    WDF_RUN_TEST(test_checkpoint_event_reevaluates_bound_group, "", "验证检查点事件重新评估已绑定分组");
    WDF_RUN_TEST(test_motion_complete_keeps_active_condition, "", "验证条件仍成立时运动结束不清除报警");
    WDF_RUN_TEST(test_unbound_trigger_is_noop, "", "验证未绑定触发源为无操作");

    return UNITY_END();
}
