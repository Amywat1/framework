/**
 * @file    test_op_mode_bridge.c
 * @brief   op_mode_bridge 运行模式事件桥接单测
 */

#include "application/op_mode_bridge.h"
#include "common/event_types.h"
#include "common/sw_error.h"
#include "common/time_util.h"
#include "domain/op_mode/device_command.h"
#include "domain/op_mode/op_mode_types.h"
#include "domain/op_mode/operational_mode.h"

#include "runtime/event_bus/event_bus.h"
#include "unity.h"

#include <pthread.h>
#include <unistd.h>

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

/* 辅助：直接将 op_mode 推进到 IDLE（绕过 event_bus）*/
static void setup_idle(void)
{
    dev_cmd_t cmd = dev_cmd_make_simple(DEV_CMD_HOME_DEVICE);

    (void)op_mode_handle_command(&cmd); /* STOPPED → HOMING */
    op_mode_on_home_completed(true);    /* HOMING → IDLE */
}

void setUp(void)
{
}

void tearDown(void)
{
}

/* 急停触发 → EXCEPTION */
static void test_hw_estop_on_enters_exception(void)
{
    pthread_t tid;

    time_util_init();
    TEST_ASSERT_EQUAL_INT(SW_OK, event_bus_init());
    TEST_ASSERT_EQUAL_INT(SW_OK, operational_mode_init());
    TEST_ASSERT_EQUAL_INT(SW_OK, op_mode_bridge_init());
    tid = start_dispatch();
    usleep(10000);

    publish_and_wait(EVT_HW_ESTOP_ON, 0U);
    TEST_ASSERT_TRUE(op_mode_is_estop_active());
    TEST_ASSERT_EQUAL_INT(OP_MODE_EXCEPTION, op_mode_get_current());

    stop_dispatch(tid);
}

/* EVT_WASH_SESSION_STARTED → WASHING */
static void test_wash_session_started_enters_washing(void)
{
    pthread_t tid;

    time_util_init();
    TEST_ASSERT_EQUAL_INT(SW_OK, event_bus_init());
    TEST_ASSERT_EQUAL_INT(SW_OK, operational_mode_init());
    TEST_ASSERT_EQUAL_INT(SW_OK, op_mode_bridge_init());
    setup_idle(); /* STOPPED → IDLE */
    tid = start_dispatch();
    usleep(10000);

    publish_and_wait(EVT_WASH_SESSION_STARTED, 0U);
    TEST_ASSERT_EQUAL_INT(OP_MODE_WASHING, op_mode_get_current());

    stop_dispatch(tid);
}

/* 正常洗车完成 → WASH_DONE */
static void test_wash_done_enters_wash_done(void)
{
    pthread_t tid;

    time_util_init();
    TEST_ASSERT_EQUAL_INT(SW_OK, event_bus_init());
    TEST_ASSERT_EQUAL_INT(SW_OK, operational_mode_init());
    TEST_ASSERT_EQUAL_INT(SW_OK, op_mode_bridge_init());
    setup_idle();
    tid = start_dispatch();
    usleep(10000);

    publish_and_wait(EVT_WASH_SESSION_STARTED, 0U);
    publish_and_wait(EVT_WASH_DONE, 0U);
    TEST_ASSERT_EQUAL_INT(OP_MODE_WASH_DONE, op_mode_get_current());

    stop_dispatch(tid);
}

/* 洗车完成 + 客户离场 → IDLE */
static void test_customer_gone_returns_idle(void)
{
    pthread_t tid;

    time_util_init();
    TEST_ASSERT_EQUAL_INT(SW_OK, event_bus_init());
    TEST_ASSERT_EQUAL_INT(SW_OK, operational_mode_init());
    TEST_ASSERT_EQUAL_INT(SW_OK, op_mode_bridge_init());
    setup_idle();
    tid = start_dispatch();
    usleep(10000);

    publish_and_wait(EVT_WASH_SESSION_STARTED, 0U);
    publish_and_wait(EVT_WASH_DONE, 0U);
    publish_and_wait(EVT_WASH_CUSTOMER_GONE, 0U);
    TEST_ASSERT_EQUAL_INT(OP_MODE_IDLE, op_mode_get_current());

    stop_dispatch(tid);
}

/* 手动停止洗车 → ALARM_HOMING */
static void test_wash_aborted_manual_enters_alarm_homing(void)
{
    pthread_t tid;

    time_util_init();
    TEST_ASSERT_EQUAL_INT(SW_OK, event_bus_init());
    TEST_ASSERT_EQUAL_INT(SW_OK, operational_mode_init());
    TEST_ASSERT_EQUAL_INT(SW_OK, op_mode_bridge_init());
    setup_idle();
    tid = start_dispatch();
    usleep(10000);

    publish_and_wait(EVT_WASH_SESSION_STARTED, 0U);
    publish_and_wait(EVT_WASH_ABORTED, wash_abort_evt_param(WASH_ABORT_MANUAL));
    TEST_ASSERT_EQUAL_INT(OP_MODE_ALARM_HOMING, op_mode_get_current());

    stop_dispatch(tid);
}

/* 自检完成（从 STOPPED 出发，成功）→ STOPPED */
static void test_self_check_from_stopped_lands_stopped(void)
{
    dev_cmd_t cmd = dev_cmd_make_simple(DEV_CMD_START_SELF_CHECK);
    pthread_t tid;

    time_util_init();
    TEST_ASSERT_EQUAL_INT(SW_OK, event_bus_init());
    TEST_ASSERT_EQUAL_INT(SW_OK, operational_mode_init());
    TEST_ASSERT_EQUAL_INT(SW_OK, op_mode_bridge_init());
    (void)op_mode_handle_command(&cmd); /* STOPPED → SELF_CHECK */
    tid = start_dispatch();
    usleep(10000);

    publish_and_wait(EVT_OP_MODE_SELF_CHECK_COMPLETED, 0U); /* land_exception=false */
    TEST_ASSERT_EQUAL_INT(OP_MODE_STOPPED, op_mode_get_current());

    stop_dispatch(tid);
}

/* EVT_OP_MODE_HOME_COMPLETED(1) → HOMING → IDLE */
static void test_home_completed_success_enters_idle(void)
{
    pthread_t tid;

    time_util_init();
    TEST_ASSERT_EQUAL_INT(SW_OK, event_bus_init());
    TEST_ASSERT_EQUAL_INT(SW_OK, operational_mode_init());
    TEST_ASSERT_EQUAL_INT(SW_OK, op_mode_bridge_init());

    /* 手动推进到 HOMING 态 */
    {
        dev_cmd_t cmd = dev_cmd_make_simple(DEV_CMD_HOME_DEVICE);
        (void)op_mode_handle_command(&cmd);
    }
    TEST_ASSERT_EQUAL_INT(OP_MODE_HOMING, op_mode_get_current());

    tid = start_dispatch();
    usleep(10000);

    publish_and_wait(EVT_OP_MODE_HOME_COMPLETED, 1U); /* 成功 */
    TEST_ASSERT_EQUAL_INT(OP_MODE_IDLE, op_mode_get_current());

    stop_dispatch(tid);
}

/* EVT_SAFETY_HOME_DONE → ALARM_HOMING → EXCEPTION */
static void test_alarm_home_done_enters_exception(void)
{
    pthread_t tid;

    time_util_init();
    TEST_ASSERT_EQUAL_INT(SW_OK, event_bus_init());
    TEST_ASSERT_EQUAL_INT(SW_OK, operational_mode_init());
    TEST_ASSERT_EQUAL_INT(SW_OK, op_mode_bridge_init());
    setup_idle();

    /* 手动推进到 ALARM_HOMING */
    op_mode_on_wash_session_started();
    op_mode_on_wash_session_aborted(WASH_ABORT_CRITICAL);
    TEST_ASSERT_EQUAL_INT(OP_MODE_ALARM_HOMING, op_mode_get_current());

    tid = start_dispatch();
    usleep(10000);

    publish_and_wait(EVT_SAFETY_HOME_DONE, 0U);
    TEST_ASSERT_EQUAL_INT(OP_MODE_EXCEPTION, op_mode_get_current());

    stop_dispatch(tid);
}

int main(void)
{
    UNITY_BEGIN();

    RUN_TEST(test_hw_estop_on_enters_exception);
    RUN_TEST(test_wash_session_started_enters_washing);
    RUN_TEST(test_wash_done_enters_wash_done);
    RUN_TEST(test_customer_gone_returns_idle);
    RUN_TEST(test_wash_aborted_manual_enters_alarm_homing);
    RUN_TEST(test_self_check_from_stopped_lands_stopped);
    RUN_TEST(test_home_completed_success_enters_idle);
    RUN_TEST(test_alarm_home_done_enters_exception);

    return UNITY_END();
}
