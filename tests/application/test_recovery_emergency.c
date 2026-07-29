/**
 * @file    test_recovery_emergency.c
 * @brief   recovery_service / safety_cutout / abort_home 协调器单元测试
 */

#include "adapters/outbound/safety/sim/hw_estop_sim.h"
#include "application/orchestrators/abort_home_coordinator.h"
#include "application/orchestrators/safety_cutout_coordinator.h"
#include "application/recovery_service.h"
#include "common/event_types.h"
#include "common/sw_error.h"
#include "common/time_util.h"
#include "domain/op_mode/op_mode_types.h"
#include "domain/safety/alarm_registry/alarm_registry.h"
#include "domain/safety/model/alarm_types.h"
#include "ports/outbound/machine/machine_ops_port.h"
#include "runtime/event_bus/event_bus.h"
#include "unity.h"

#include <pthread.h>
#include <string.h>
#include <unistd.h>

static volatile int                s_recovery_completed_count;
static volatile uint32_t           s_recovery_result_param;
static volatile int                s_abort_home_count;
static volatile int                s_home_device_count;
static volatile int                s_abort_count;
static volatile wash_abort_cause_t s_abort_cause;
static volatile int                s_deferred_stop_count;
static uint32_t                    s_clear_on_home_code;

#define TEST_BLOCKING_ALARM_CODE 201101U
#define TEST_LOCKOUT_ALARM_CODE  201102U

void safety_deferred_stop(void)
{
    s_deferred_stop_count++;
}

static void stub_abort_wash(wash_abort_cause_t cause)
{
    s_abort_count++;
    s_abort_cause = cause;
}

static void stub_abort_home(void)
{
    s_abort_home_count++;
    (void)event_publish(EVT_ABORT_HOME_DONE, (uint32_t)SW_OK);
}

static sw_err_t stub_home_device(void)
{
    s_home_device_count++;
    if (s_clear_on_home_code != 0U) {
        (void)alarm_registry_clear(s_clear_on_home_code);
    }
    (void)event_publish(EVT_OP_MODE_HOME_COMPLETED, 1U);
    return SW_OK;
}

static machine_ops_t s_machine_ops;

static const alarm_def_t s_blocking_catalog[] = {
    {
     .code         = TEST_BLOCKING_ALARM_CODE,
     .level        = ALARM_LEVEL_MAJOR,
     .clear        = ALARM_CLEAR_MANUAL_RESET,
     .reeval_group = ALARM_REEVAL_GROUP_NONE,
     .desc         = "test blocking",
     },
};

static const alarm_def_t s_lockout_catalog[] = {
    {
     .code         = TEST_LOCKOUT_ALARM_CODE,
     .level        = ALARM_LEVEL_CRITICAL,
     .clear        = ALARM_CLEAR_MANUAL_RESET,
     .reeval_group = ALARM_REEVAL_GROUP_NONE,
     .desc         = "test lockout",
     },
};

static void on_recovery_completed(const event_t *evt)
{
    s_recovery_completed_count++;
    s_recovery_result_param = evt->param;
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
    s_recovery_completed_count = 0;
    s_recovery_result_param    = 0U;
    s_abort_home_count         = 0;
    s_home_device_count        = 0;
    s_abort_count              = 0;
    s_abort_cause              = WASH_ABORT_MANUAL;
    s_deferred_stop_count      = 0;
    s_clear_on_home_code       = 0U;

    memset(&s_machine_ops, 0, sizeof(s_machine_ops));
    s_machine_ops.abort_home  = stub_abort_home;
    s_machine_ops.home_device = stub_home_device;
    s_machine_ops.abort_wash  = stub_abort_wash;

    time_util_init();
}

void tearDown(void)
{
}

static void test_recovery_service_publishes_completed_idle(void)
{
    pthread_t tid;

    TEST_ASSERT_EQUAL_INT(SW_OK, event_bus_init());
    TEST_ASSERT_EQUAL_INT(SW_OK, alarm_registry_init());
    machine_ops_register(&s_machine_ops);
    TEST_ASSERT_EQUAL_INT(SW_OK, recovery_service_init());
    TEST_ASSERT_EQUAL_INT(SW_OK, event_subscribe(EVT_OP_MODE_RECOVERY_COMPLETED, on_recovery_completed));

    tid = start_dispatch();
    TEST_ASSERT_EQUAL_INT(SW_OK, event_publish(EVT_OP_MODE_RECOVERY_REQUESTED, 0U));
    usleep(50000U);

    TEST_ASSERT_EQUAL_INT(1, s_recovery_completed_count);
    TEST_ASSERT_EQUAL_UINT32((uint32_t)RECOVERY_RESULT_IDLE, s_recovery_result_param);
    TEST_ASSERT_EQUAL_INT(1, s_home_device_count);
    stop_dispatch(tid);
}

static void test_recovery_service_keeps_exception_when_blocking_remains(void)
{
    pthread_t tid;

    TEST_ASSERT_EQUAL_INT(SW_OK, event_bus_init());
    TEST_ASSERT_EQUAL_INT(SW_OK, alarm_registry_init());
    TEST_ASSERT_EQUAL_INT(SW_OK, alarm_registry_load_catalog(s_blocking_catalog, 1U));
    TEST_ASSERT_EQUAL_INT(SW_OK, alarm_registry_trigger(TEST_BLOCKING_ALARM_CODE));
    machine_ops_register(&s_machine_ops);
    TEST_ASSERT_EQUAL_INT(SW_OK, recovery_service_init());
    TEST_ASSERT_EQUAL_INT(SW_OK, event_subscribe(EVT_OP_MODE_RECOVERY_COMPLETED, on_recovery_completed));

    tid = start_dispatch();
    TEST_ASSERT_EQUAL_INT(SW_OK, event_publish(EVT_OP_MODE_RECOVERY_REQUESTED, 0U));
    usleep(50000U);

    TEST_ASSERT_EQUAL_INT(1, s_recovery_completed_count);
    TEST_ASSERT_EQUAL_UINT32((uint32_t)RECOVERY_RESULT_EXCEPTION, s_recovery_result_param);
    TEST_ASSERT_EQUAL_INT(1, s_home_device_count);
    stop_dispatch(tid);
}

static void test_recovery_resets_blocking_alarm_cleared_during_home(void)
{
    pthread_t tid;

    TEST_ASSERT_EQUAL_INT(SW_OK, event_bus_init());
    TEST_ASSERT_EQUAL_INT(SW_OK, alarm_registry_init());
    TEST_ASSERT_EQUAL_INT(SW_OK, alarm_registry_load_catalog(s_blocking_catalog, 1U));
    TEST_ASSERT_EQUAL_INT(SW_OK, alarm_registry_trigger(TEST_BLOCKING_ALARM_CODE));
    s_clear_on_home_code = TEST_BLOCKING_ALARM_CODE;
    machine_ops_register(&s_machine_ops);
    TEST_ASSERT_EQUAL_INT(SW_OK, recovery_service_init());
    TEST_ASSERT_EQUAL_INT(SW_OK, event_subscribe(EVT_OP_MODE_RECOVERY_COMPLETED, on_recovery_completed));

    tid = start_dispatch();
    TEST_ASSERT_EQUAL_INT(SW_OK, event_publish(EVT_OP_MODE_RECOVERY_REQUESTED, 0U));
    usleep(50000U);

    TEST_ASSERT_EQUAL_INT(1, s_recovery_completed_count);
    TEST_ASSERT_EQUAL_UINT32((uint32_t)RECOVERY_RESULT_IDLE, s_recovery_result_param);
    TEST_ASSERT_FALSE(alarm_registry_is_active(TEST_BLOCKING_ALARM_CODE));
    TEST_ASSERT_EQUAL_INT(1, s_home_device_count);
    stop_dispatch(tid);
}

static void test_recovery_resets_inactive_lockout_before_home(void)
{
    pthread_t tid;

    TEST_ASSERT_EQUAL_INT(SW_OK, event_bus_init());
    TEST_ASSERT_EQUAL_INT(SW_OK, alarm_registry_init());
    TEST_ASSERT_EQUAL_INT(SW_OK, alarm_registry_load_catalog(s_lockout_catalog, 1U));
    TEST_ASSERT_EQUAL_INT(SW_OK, alarm_registry_trigger(TEST_LOCKOUT_ALARM_CODE));
    TEST_ASSERT_EQUAL_INT(SW_OK, alarm_registry_clear(TEST_LOCKOUT_ALARM_CODE));
    TEST_ASSERT_EQUAL_INT(SAFETY_POSTURE_LOCKOUT, alarm_registry_safety_posture());
    machine_ops_register(&s_machine_ops);
    TEST_ASSERT_EQUAL_INT(SW_OK, recovery_service_init());
    TEST_ASSERT_EQUAL_INT(SW_OK, event_subscribe(EVT_OP_MODE_RECOVERY_COMPLETED, on_recovery_completed));

    tid = start_dispatch();
    TEST_ASSERT_EQUAL_INT(SW_OK, event_publish(EVT_OP_MODE_RECOVERY_REQUESTED, 0U));
    usleep(50000U);

    TEST_ASSERT_EQUAL_INT(1, s_recovery_completed_count);
    TEST_ASSERT_EQUAL_UINT32((uint32_t)RECOVERY_RESULT_IDLE, s_recovery_result_param);
    TEST_ASSERT_FALSE(alarm_registry_is_active(TEST_LOCKOUT_ALARM_CODE));
    TEST_ASSERT_EQUAL_INT(1, s_home_device_count);
    stop_dispatch(tid);
}

static void test_cutout_estop_release_does_not_run_abort_home(void)
{
    pthread_t tid;

    TEST_ASSERT_EQUAL_INT(SW_OK, event_bus_init());
    machine_ops_register(&s_machine_ops);
    TEST_ASSERT_EQUAL_INT(SW_OK, safety_cutout_coordinator_init());
    TEST_ASSERT_EQUAL_INT(SW_OK, abort_home_coordinator_init());

    tid = start_dispatch();
    TEST_ASSERT_EQUAL_INT(SW_OK, event_publish(EVT_HW_ESTOP_OFF, 0U));
    usleep(50000U);

    TEST_ASSERT_EQUAL_INT(0, s_abort_home_count);
    stop_dispatch(tid);
}

static void test_cutout_lockout_aborts_wash(void)
{
    pthread_t tid;

    TEST_ASSERT_EQUAL_INT(SW_OK, event_bus_init());
    machine_ops_register(&s_machine_ops);
    TEST_ASSERT_EQUAL_INT(SW_OK, safety_cutout_coordinator_init());

    tid = start_dispatch();
    TEST_ASSERT_EQUAL_INT(SW_OK, event_publish(EVT_SAFETY_LOCKOUT, 0U));
    usleep(50000U);

    TEST_ASSERT_EQUAL_INT(1, s_abort_count);
    TEST_ASSERT_EQUAL_INT(WASH_ABORT_CRITICAL, s_abort_cause);
    TEST_ASSERT_EQUAL_INT(0, s_deferred_stop_count);
    stop_dispatch(tid);
}

static void test_abort_home_requested_runs_abort_home(void)
{
    pthread_t tid;

    TEST_ASSERT_EQUAL_INT(SW_OK, event_bus_init());
    machine_ops_register(&s_machine_ops);
    TEST_ASSERT_EQUAL_INT(SW_OK, abort_home_coordinator_init());

    tid = start_dispatch();
    TEST_ASSERT_EQUAL_INT(SW_OK, event_publish(EVT_ABORT_HOME_REQUESTED, 0U));
    usleep(50000U);

    TEST_ASSERT_EQUAL_INT(1, s_abort_home_count);
    stop_dispatch(tid);
}

static void test_cutout_estop_on_aborts_wash_and_defers_stop(void)
{
    pthread_t tid;

    TEST_ASSERT_EQUAL_INT(SW_OK, event_bus_init());
    machine_ops_register(&s_machine_ops);
    TEST_ASSERT_EQUAL_INT(SW_OK, safety_cutout_coordinator_init());
    TEST_ASSERT_EQUAL_INT(SW_OK, abort_home_coordinator_init());

    tid = start_dispatch();
    TEST_ASSERT_EQUAL_INT(SW_OK, event_publish(EVT_HW_ESTOP_ON, 0U));
    usleep(50000U);

    TEST_ASSERT_EQUAL_INT(1, s_deferred_stop_count);
    TEST_ASSERT_EQUAL_INT(1, s_abort_count);
    TEST_ASSERT_EQUAL_INT(WASH_ABORT_ESTOP, s_abort_cause);
    TEST_ASSERT_EQUAL_INT(0, s_abort_home_count);
    stop_dispatch(tid);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_recovery_service_publishes_completed_idle);
    RUN_TEST(test_recovery_service_keeps_exception_when_blocking_remains);
    RUN_TEST(test_recovery_resets_blocking_alarm_cleared_during_home);
    RUN_TEST(test_recovery_resets_inactive_lockout_before_home);
    RUN_TEST(test_cutout_estop_release_does_not_run_abort_home);
    RUN_TEST(test_cutout_lockout_aborts_wash);
    RUN_TEST(test_abort_home_requested_runs_abort_home);
    RUN_TEST(test_cutout_estop_on_aborts_wash_and_defers_stop);
    return UNITY_END();
}
