/**
 * @file    test_command_gateway.c
 * @brief   command_gateway 鍛戒护缃戝叧鍗曞厓娴嬭瘯
 */

#include "application/command_gateway.h"
#include "common/sw_error.h"
#include "domain/command_gateway/command_types.h"
#include "domain/command_gateway/device_command.h"
#include "domain/command_gateway/operational_mode.h"

#include "ports/inbound/command/command_port.h"
#include "runtime/event_bus/event_bus.h"
#include "tests/stubs/wash_orchestrator_stub.h"
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

static sw_err_t submit_simple(dev_cmd_kind_t kind, dev_cmd_receipt_t *receipt)
{
    dev_cmd_t cmd = dev_cmd_make_simple(kind);

    return device_command_port_get_ops()->submit(&cmd, receipt, 1000U);
}

void setUp(void)
{
    wash_orchestrator_stub_reset();
}

void tearDown(void)
{
}

static void test_submit_accepted_in_idle(void)
{
    dev_cmd_receipt_t receipt = {0};
    pthread_t         tid;

    TEST_ASSERT_EQUAL_INT(SW_OK, event_bus_init());
    TEST_ASSERT_EQUAL_INT(SW_OK, operational_mode_init());
    TEST_ASSERT_EQUAL_INT(SW_OK, command_gateway_init());
    tid = start_dispatch();
    usleep(30000);

    TEST_ASSERT_EQUAL_INT(SW_OK, submit_simple(DEV_CMD_STOP_OPERATION, &receipt));
    TEST_ASSERT_EQUAL_INT(DEV_CMD_STATUS_ACCEPTED, receipt.status);

    stop_dispatch(tid);
}

static void test_submit_rejected_wrong_mode(void)
{
    dev_cmd_receipt_t receipt = {0};
    pthread_t         tid;

    TEST_ASSERT_EQUAL_INT(SW_OK, event_bus_init());
    TEST_ASSERT_EQUAL_INT(SW_OK, operational_mode_init());
    TEST_ASSERT_EQUAL_INT(SW_OK, command_gateway_init());
    tid = start_dispatch();

    TEST_ASSERT_EQUAL_INT(SW_ERR_STATE, submit_simple(DEV_CMD_STOP_WASH, &receipt));
    TEST_ASSERT_EQUAL_INT(DEV_CMD_STATUS_REJECTED, receipt.status);

    stop_dispatch(tid);
}

static void test_enter_manual_changes_mode(void)
{
    dev_cmd_receipt_t receipt = {0};
    pthread_t         tid;

    TEST_ASSERT_EQUAL_INT(SW_OK, event_bus_init());
    TEST_ASSERT_EQUAL_INT(SW_OK, operational_mode_init());
    TEST_ASSERT_EQUAL_INT(SW_OK, command_gateway_init());
    tid = start_dispatch();
    usleep(30000);

    TEST_ASSERT_EQUAL_INT(SW_OK, submit_simple(DEV_CMD_ENTER_MANUAL, &receipt));
    TEST_ASSERT_EQUAL_INT(DEV_CMD_STATUS_ACCEPTED, receipt.status);
    TEST_ASSERT_EQUAL_INT(OP_MODE_MANUAL, op_mode_get_current());

    stop_dispatch(tid);
}

static void test_start_wash_triggers_orchestrator(void)
{
    dev_cmd_t         cmd     = dev_cmd_make_start_wash(WASH_MODE_STANDARD);
    dev_cmd_receipt_t receipt = {0};
    pthread_t         tid;

    TEST_ASSERT_EQUAL_INT(SW_OK, event_bus_init());
    TEST_ASSERT_EQUAL_INT(SW_OK, operational_mode_init());
    TEST_ASSERT_EQUAL_INT(SW_OK, command_gateway_init());
    tid = start_dispatch();
    usleep(30000);

    TEST_ASSERT_EQUAL_INT(SW_OK, device_command_port_get_ops()->submit(&cmd, &receipt, 1000U));
    TEST_ASSERT_EQUAL_INT(DEV_CMD_STATUS_ACCEPTED, receipt.status);
    TEST_ASSERT_EQUAL_INT(1, wash_orchestrator_stub_start_count());
    TEST_ASSERT_EQUAL_INT(WASH_MODE_STANDARD, wash_orchestrator_stub_last_mode());

    stop_dispatch(tid);
}

static void test_stop_operation_then_resume(void)
{
    dev_cmd_receipt_t receipt = {0};
    pthread_t         tid;

    TEST_ASSERT_EQUAL_INT(SW_OK, event_bus_init());
    TEST_ASSERT_EQUAL_INT(SW_OK, operational_mode_init());
    TEST_ASSERT_EQUAL_INT(SW_OK, command_gateway_init());
    tid = start_dispatch();
    usleep(30000);

    TEST_ASSERT_EQUAL_INT(SW_OK, submit_simple(DEV_CMD_STOP_OPERATION, &receipt));
    TEST_ASSERT_FALSE(op_mode_is_service_enabled());

    TEST_ASSERT_EQUAL_INT(SW_OK, submit_simple(DEV_CMD_RESUME_OPERATION, &receipt));
    TEST_ASSERT_EQUAL_INT(DEV_CMD_STATUS_ACCEPTED, receipt.status);
    TEST_ASSERT_TRUE(op_mode_is_service_enabled());

    stop_dispatch(tid);
}

static void test_port_not_registered_before_init(void)
{
    TEST_ASSERT_NULL(device_command_port_get_ops());
}

int main(void)
{
    UNITY_BEGIN();

    RUN_TEST(test_port_not_registered_before_init);
    RUN_TEST(test_submit_accepted_in_idle);
    RUN_TEST(test_submit_rejected_wrong_mode);
    RUN_TEST(test_enter_manual_changes_mode);
    RUN_TEST(test_start_wash_triggers_orchestrator);
    RUN_TEST(test_stop_operation_then_resume);

    return UNITY_END();
}
