/**
 * @file    test_op_mode_bridge.c
 * @brief   op_mode_bridge 运行模式事件桥接单测
 */

#include "application/op_mode_bridge.h"
#include "common/event_types.h"
#include "common/sw_error.h"
#include "common/time_util.h"
#include "domain/command_gateway/device_command.h"
#include "domain/command_gateway/op_mode_types.h"
#include "domain/command_gateway/operational_mode.h"

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

void setUp(void)
{
}

void tearDown(void)
{
}

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

static void test_wash_session_started_enters_washing(void)
{
    pthread_t tid;

    time_util_init();
    TEST_ASSERT_EQUAL_INT(SW_OK, event_bus_init());
    TEST_ASSERT_EQUAL_INT(SW_OK, operational_mode_init());
    TEST_ASSERT_EQUAL_INT(SW_OK, op_mode_bridge_init());
    tid = start_dispatch();
    usleep(10000);

    publish_and_wait(EVT_WASH_SESSION_STARTED, 0U);
    TEST_ASSERT_EQUAL_INT(OP_MODE_WASHING, op_mode_get_current());

    stop_dispatch(tid);
}

static void test_wash_done_returns_idle(void)
{
    pthread_t tid;

    time_util_init();
    TEST_ASSERT_EQUAL_INT(SW_OK, event_bus_init());
    TEST_ASSERT_EQUAL_INT(SW_OK, operational_mode_init());
    TEST_ASSERT_EQUAL_INT(SW_OK, op_mode_bridge_init());
    tid = start_dispatch();
    usleep(10000);

    publish_and_wait(EVT_WASH_SESSION_STARTED, 0U);
    publish_and_wait(EVT_WASH_DONE, 0U);
    TEST_ASSERT_EQUAL_INT(OP_MODE_IDLE, op_mode_get_current());

    stop_dispatch(tid);
}

static void test_wash_aborted_manual_returns_idle(void)
{
    pthread_t tid;

    time_util_init();
    TEST_ASSERT_EQUAL_INT(SW_OK, event_bus_init());
    TEST_ASSERT_EQUAL_INT(SW_OK, operational_mode_init());
    TEST_ASSERT_EQUAL_INT(SW_OK, op_mode_bridge_init());
    tid = start_dispatch();
    usleep(10000);

    publish_and_wait(EVT_WASH_SESSION_STARTED, 0U);
    publish_and_wait(EVT_WASH_ABORTED, wash_abort_evt_param(WASH_ABORT_MANUAL));
    TEST_ASSERT_EQUAL_INT(OP_MODE_IDLE, op_mode_get_current());

    stop_dispatch(tid);
}

static void test_self_check_completed_lands_idle(void)
{
    dev_cmd_t cmd = dev_cmd_make_simple(DEV_CMD_START_SELF_CHECK);
    pthread_t tid;

    time_util_init();
    TEST_ASSERT_EQUAL_INT(SW_OK, event_bus_init());
    TEST_ASSERT_EQUAL_INT(SW_OK, operational_mode_init());
    TEST_ASSERT_EQUAL_INT(SW_OK, op_mode_bridge_init());
    (void)op_mode_handle_command(&cmd);
    tid = start_dispatch();
    usleep(10000);

    publish_and_wait(EVT_OP_MODE_SELF_CHECK_COMPLETED, 0U);
    TEST_ASSERT_EQUAL_INT(OP_MODE_IDLE, op_mode_get_current());

    stop_dispatch(tid);
}

int main(void)
{
    UNITY_BEGIN();

    RUN_TEST(test_hw_estop_on_enters_exception);
    RUN_TEST(test_wash_session_started_enters_washing);
    RUN_TEST(test_wash_done_returns_idle);
    RUN_TEST(test_wash_aborted_manual_returns_idle);
    RUN_TEST(test_self_check_completed_lands_idle);

    return UNITY_END();
}
