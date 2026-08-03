/**
 * @file    test_alarm_reeval_bridge.c
 * @brief   alarm_reeval_bridge 鍗曞厓娴嬭瘯
 */

#include "adapters/inbound/event/alarm_reeval_bridge.h"
#include "common/sw_error.h"
#include "common/time_util.h"
#include "domain/device_control/model/actuator_events.h"
#include "domain/safety/alarm_registry/alarm_registry.h"
#include "domain/safety/model/alarm_types.h"
#include "runtime/event_bus/event_bus.h"
#include "unity.h"

#include <pthread.h>
#include <unistd.h>

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

    TEST_ASSERT_EQUAL_INT(SW_ERR_PARAM, alarm_reeval_bridge_init(NULL, 1U));
    TEST_ASSERT_EQUAL_INT(SW_ERR_PARAM, alarm_reeval_bridge_init(zero_trigger, 1U));
    TEST_ASSERT_EQUAL_INT(SW_ERR_PARAM, alarm_reeval_bridge_init(no_group, 1U));
    TEST_ASSERT_EQUAL_INT(SW_ERR_PARAM, alarm_reeval_bridge_init(duplicate, 2U));
}

static void test_actuator_event_reevaluates_bound_group_only(void)
{
    pthread_t tid;

    TEST_ASSERT_EQUAL_INT(SW_OK, alarm_reeval_bridge_init(s_bindings, 2U));
    TEST_ASSERT_EQUAL_INT(SW_OK, alarm_registry_trigger(201105U));
    TEST_ASSERT_EQUAL_INT(SW_OK, alarm_registry_trigger(201205U));
    TEST_ASSERT_TRUE(alarm_registry_is_active(201105U));
    TEST_ASSERT_TRUE(alarm_registry_is_active(201205U));

    tid = start_dispatch();
    actuator_publish_motion_completed(TEST_ACTUATOR_GANTRY);
    usleep(50000);

    TEST_ASSERT_FALSE(alarm_registry_is_active(201105U));
    TEST_ASSERT_TRUE(alarm_registry_is_active(201205U));
    stop_dispatch(tid);
}

static void test_checkpoint_event_reevaluates_bound_group(void)
{
    pthread_t tid;

    TEST_ASSERT_EQUAL_INT(SW_OK, alarm_reeval_bridge_init(s_bindings, 2U));
    TEST_ASSERT_EQUAL_INT(SW_OK, alarm_registry_trigger(201205U));

    tid = start_dispatch();
    wash_publish_checkpoint_reached(TEST_CHECKPOINT_EXIT);
    usleep(50000);

    TEST_ASSERT_FALSE(alarm_registry_is_active(201205U));
    stop_dispatch(tid);
}

static void test_unbound_trigger_is_noop(void)
{
    TEST_ASSERT_EQUAL_INT(SW_OK, alarm_reeval_bridge_init(s_bindings, 2U));
    TEST_ASSERT_EQUAL_INT(SW_OK, alarm_registry_trigger(201105U));

    TEST_ASSERT_EQUAL_INT(SW_OK, alarm_reeval_bridge_handle(ALARM_REEVAL_TRIGGER_ACTUATOR_COMPLETED, 99U));
    TEST_ASSERT_TRUE(alarm_registry_is_active(201105U));
}

int main(void)
{
    UNITY_BEGIN();

    RUN_TEST(test_init_rejects_invalid_binding_table);
    RUN_TEST(test_actuator_event_reevaluates_bound_group_only);
    RUN_TEST(test_checkpoint_event_reevaluates_bound_group);
    RUN_TEST(test_unbound_trigger_is_noop);

    return UNITY_END();
}
