/**
 * @file    test_telemetry_projection.c
 * @brief   telemetry snapshot/projection 鍗曞厓娴嬭瘯
 */

#include "application/telemetry_projection.h"
#include "common/event_types.h"
#include "common/sw_error.h"
#include "common/time_util.h"
#include "domain/op_mode/device_command.h"
#include "domain/op_mode/operational_mode.h"
#include "domain/safety/alarm_registry/alarm_registry.h"
#include "domain/telemetry/device_snapshot.h"
#include "domain/telemetry/device_snapshot_internal.h"
#include "runtime/event_bus/event_bus.h"
#include "unity.h"

#include <pthread.h>
#include <unistd.h>

static const alarm_def_t s_catalog[] = {
    {
     .code             = 201101U,
     .level            = ALARM_LEVEL_MAJOR,
     .clear            = ALARM_CLEAR_MANUAL_RESET,
     .reeval_group     = ALARM_REEVAL_GROUP_NONE,
     .desc             = "blocking alarm",
     },
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

static void publish_and_wait(event_type_t type, uint32_t param)
{
    TEST_ASSERT_EQUAL_INT(SW_OK, event_publish(type, param));
    usleep(50000);
}

void setUp(void)
{
}

void tearDown(void)
{
}

static void test_snapshot_direct_updates_are_read_back(void)
{
    operational_snapshot_t op = {
        .mode            = OP_MODE_IDLE,
        .service_enabled = true,
        .estop_active    = false,
    };
    safety_snapshot_t safety = {
        .posture         = SAFETY_POSTURE_LOCKOUT,
        .blocking_active = false,
    };

    device_snapshot_update_op(&op);
    device_snapshot_update_safety(&safety);
    device_snapshot_set_wash_mode(WASH_MODE_QUICK);

    {
        operational_snapshot_t s = operational_snapshot_get();
        TEST_ASSERT_TRUE(operational_snapshot_is_standby(s));
        TEST_ASSERT_FALSE(operational_snapshot_is_stopping(s));
    }
    TEST_ASSERT_TRUE(safety_snapshot_is_warning_active());
    TEST_ASSERT_EQUAL_INT(WASH_MODE_QUICK, wash_snapshot_get().mode);
}

static void test_wash_projection_tracks_session_started_event(void)
{
    pthread_t tid;

    time_util_init();
    TEST_ASSERT_EQUAL_INT(SW_OK, event_bus_init());
    TEST_ASSERT_EQUAL_INT(SW_OK, telemetry_projection_init());
    tid = start_dispatch();
    usleep(10000);

    publish_and_wait(EVT_WASH_SESSION_STARTED, (uint32_t)WASH_MODE_QUICK);
    TEST_ASSERT_EQUAL_INT(WASH_MODE_QUICK, wash_snapshot_get().mode);

    stop_dispatch(tid);
}

static void test_operational_projection_syncs_current_context(void)
{
    pthread_t              tid;
    operational_snapshot_t snap;

    time_util_init();
    TEST_ASSERT_EQUAL_INT(SW_OK, event_bus_init());
    TEST_ASSERT_EQUAL_INT(SW_OK, operational_mode_init());
    TEST_ASSERT_EQUAL_INT(SW_OK, telemetry_projection_init());
    tid = start_dispatch();
    usleep(10000);

    snap = operational_snapshot_get();
    TEST_ASSERT_EQUAL_INT(OP_MODE_IDLE, snap.mode);
    TEST_ASSERT_TRUE(snap.service_enabled);
    TEST_ASSERT_TRUE(operational_snapshot_is_standby(snap));

    op_mode_set_service_enabled(false);
    publish_and_wait(EVT_OP_MODE_CONTEXT_SYNC, 0U);
    snap = operational_snapshot_get();
    TEST_ASSERT_FALSE(snap.service_enabled);
    TEST_ASSERT_TRUE(operational_snapshot_is_stopping(snap));

    stop_dispatch(tid);
}

static void test_safety_projection_refreshes_alarm_snapshot(void)
{
    pthread_t         tid;
    safety_snapshot_t snap;

    time_util_init();
    TEST_ASSERT_EQUAL_INT(SW_OK, event_bus_init());
    TEST_ASSERT_EQUAL_INT(SW_OK, alarm_registry_init());
    TEST_ASSERT_EQUAL_INT(SW_OK, alarm_registry_load_catalog(s_catalog, 1U));
    TEST_ASSERT_EQUAL_INT(SW_OK, telemetry_projection_init());
    tid = start_dispatch();
    usleep(10000);

    TEST_ASSERT_EQUAL_INT(SW_OK, alarm_registry_trigger(201101U));
    publish_and_wait(EVT_ALARM_TRIGGERED, 201101U);

    snap = safety_snapshot_get();
    TEST_ASSERT_TRUE(snap.blocking_active);
    TEST_ASSERT_EQUAL_UINT(1U, snap.active_alarm_count);
    TEST_ASSERT_EQUAL_UINT(201101U, snap.top_alarm_code);
    TEST_ASSERT_TRUE(safety_snapshot_is_warning_active());

    stop_dispatch(tid);
}

int main(void)
{
    UNITY_BEGIN();

    RUN_TEST(test_snapshot_direct_updates_are_read_back);
    RUN_TEST(test_wash_projection_tracks_session_started_event);
    RUN_TEST(test_operational_projection_syncs_current_context);
    RUN_TEST(test_safety_projection_refreshes_alarm_snapshot);

    return UNITY_END();
}
