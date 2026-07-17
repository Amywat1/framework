/**
 * @file    test_recovery_emergency.c
 * @brief   recovery_service / emergency_handler 单元测试
 */

#include "adapters/outbound/safety/sim/hw_estop_sim.h"
#include "application/orchestrators/emergency_handler.h"
#include "application/orchestrators/wash_orchestrator.h"
#include "application/recovery_service.h"
#include "common/event_types.h"
#include "common/sw_error.h"
#include "common/time_util.h"
#include "domain/op_mode/op_mode_types.h"
#include "domain/safety/alarm_registry/alarm_registry.h"
#include "ports/outbound/machine/machine_ops_port.h"
#include "runtime/event_bus/event_bus.h"
#include "unity.h"

#include <pthread.h>
#include <unistd.h>

static volatile int                s_recovery_completed_count;
static volatile uint32_t           s_recovery_result_param;
static volatile int                s_safety_home_count;
static volatile int                s_home_device_count;
static volatile int                s_abort_count;
static volatile wash_abort_cause_t s_abort_cause;
static volatile int                s_deferred_stop_count;

void safety_deferred_stop(void)
{
    s_deferred_stop_count++;
}

sw_err_t wash_orchestrator_init(void)
{
    return SW_OK;
}

sw_err_t wash_orchestrator_start(wash_mode_t mode)
{
    (void)mode;
    return SW_OK;
}

void wash_orchestrator_abort(wash_abort_cause_t cause)
{
    s_abort_count++;
    s_abort_cause = cause;
}

bool wash_orchestrator_is_busy(void)
{
    return false;
}

engine_direction_t wash_orchestrator_current_direction(void)
{
    return ENGINE_DIR_NONE;
}

static void stub_safety_home(void)
{
    s_safety_home_count++;
}

static sw_err_t stub_home_device(void)
{
    s_home_device_count++;
    return SW_OK;
}

static const machine_ops_t s_machine_ops = {
    .deferred_stop_all       = NULL,
    .safety_home             = stub_safety_home,
    .home_device             = stub_home_device,
    .execute_manual_actuator = NULL,
    .stop_all_outputs        = NULL,
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
    s_safety_home_count        = 0;
    s_home_device_count        = 0;
    s_abort_count              = 0;
    s_abort_cause              = WASH_ABORT_MANUAL;
    s_deferred_stop_count      = 0;
    hw_estop_sim_set_active(false);
    time_util_init();
}

void tearDown(void)
{
}

/* recovery_service：清告警 + 归位成功 → IDLE */
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

/* 急停释放后不自动执行安全归位 */
static void test_emergency_handler_estop_release_does_not_run_safety_home(void)
{
    pthread_t tid;

    TEST_ASSERT_EQUAL_INT(SW_OK, event_bus_init());
    machine_ops_register(&s_machine_ops);
    TEST_ASSERT_EQUAL_INT(SW_OK, emergency_handler_init());

    tid = start_dispatch();
    TEST_ASSERT_EQUAL_INT(SW_OK, event_publish(EVT_HW_ESTOP_OFF, 0U));
    usleep(50000U);

    /* 新设计：急停释放不再自动归位，由工作人员手动操作或执行 RECOVER */
    TEST_ASSERT_EQUAL_INT(0, s_safety_home_count);
    TEST_ASSERT_EQUAL_INT(0, s_abort_count);
    stop_dispatch(tid);
}

/* LOCKOUT → 中止洗车，不直接归位（归位由 ALARM_HOMING 路径触发）*/
static void test_emergency_handler_lockout_aborts_wash(void)
{
    pthread_t tid;

    TEST_ASSERT_EQUAL_INT(SW_OK, event_bus_init());
    machine_ops_register(&s_machine_ops);
    hw_estop_sim_set_active(false);
    TEST_ASSERT_EQUAL_INT(SW_OK, emergency_handler_init());

    tid = start_dispatch();
    TEST_ASSERT_EQUAL_INT(SW_OK, event_publish(EVT_SAFETY_LOCKOUT, 0U));
    usleep(50000U);

    TEST_ASSERT_EQUAL_INT(1, s_abort_count);
    TEST_ASSERT_EQUAL_INT(WASH_ABORT_CRITICAL, s_abort_cause);
    /* 直接归位已移至 ALARM_HOME_REQUESTED 路径，此处不再调用 */
    TEST_ASSERT_EQUAL_INT(0, s_safety_home_count);
    stop_dispatch(tid);
}

/* EVT_OP_MODE_ALARM_HOME_REQUESTED → emergency_handler 执行安全归位 */
static void test_emergency_handler_alarm_home_requested_runs_safety_home(void)
{
    pthread_t tid;

    TEST_ASSERT_EQUAL_INT(SW_OK, event_bus_init());
    machine_ops_register(&s_machine_ops);
    TEST_ASSERT_EQUAL_INT(SW_OK, emergency_handler_init());

    tid = start_dispatch();
    TEST_ASSERT_EQUAL_INT(SW_OK, event_publish(EVT_OP_MODE_ALARM_HOME_REQUESTED, 0U));
    usleep(50000U);

    TEST_ASSERT_EQUAL_INT(1, s_safety_home_count);
    stop_dispatch(tid);
}

/* 急停触发 → deferred_stop + 通知洗车中止（原因 ESTOP）*/
static void test_emergency_handler_estop_on_aborts_wash_and_defers_stop(void)
{
    pthread_t tid;

    TEST_ASSERT_EQUAL_INT(SW_OK, event_bus_init());
    machine_ops_register(&s_machine_ops);
    TEST_ASSERT_EQUAL_INT(SW_OK, emergency_handler_init());

    tid = start_dispatch();
    TEST_ASSERT_EQUAL_INT(SW_OK, event_publish(EVT_HW_ESTOP_ON, 0U));
    usleep(50000U);

    TEST_ASSERT_EQUAL_INT(1, s_deferred_stop_count);
    TEST_ASSERT_EQUAL_INT(1, s_abort_count);
    TEST_ASSERT_EQUAL_INT(WASH_ABORT_ESTOP, s_abort_cause);
    TEST_ASSERT_EQUAL_INT(0, s_safety_home_count);
    stop_dispatch(tid);
}

int main(void)
{
    UNITY_BEGIN();

    RUN_TEST(test_recovery_service_publishes_completed_idle);
    RUN_TEST(test_emergency_handler_estop_release_does_not_run_safety_home);
    RUN_TEST(test_emergency_handler_lockout_aborts_wash);
    RUN_TEST(test_emergency_handler_alarm_home_requested_runs_safety_home);
    RUN_TEST(test_emergency_handler_estop_on_aborts_wash_and_defers_stop);

    return UNITY_END();
}
