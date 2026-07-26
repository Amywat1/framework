/**
 * @file    test_command_gateway.c
 * @brief   command_gateway 命令网关单元测试
 */

#include "application/command_gateway.h"
#include "common/sw_error.h"
#include "common/trace_context.h"
#include "domain/op_mode/command_types.h"
#include "domain/op_mode/device_command.h"
#include "domain/op_mode/operational_mode.h"
#include "ports/inbound/command/command_port.h"
#include "ports/outbound/machine/machine_ops_port.h"
#include "runtime/event_bus/event_bus.h"
#include "tests/stubs/wash_ops_stub.h"
#include "unity.h"

#include <pthread.h>
#include <string.h>
#include <unistd.h>

static volatile uint64_t s_handled_command_id;
static volatile uint64_t s_handled_correlation_id;


static sw_err_t stub_home_device(void)
{
    return SW_OK;
}

static machine_ops_t s_ops;

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

static void handled_handler(const event_t *evt)
{
    s_handled_command_id     = evt->trace.command_id;
    s_handled_correlation_id = evt->trace.correlation_id;
}

static sw_err_t submit_simple(dev_cmd_kind_t kind, dev_cmd_receipt_t *receipt)
{
    dev_cmd_t cmd = dev_cmd_make_simple(kind);

    return device_command_port_get_ops()->submit(&cmd, receipt, 1000U);
}

/* 辅助：把 op_mode 直接推进到 IDLE（绕过 event_bus）*/
static void setup_idle(void)
{
    dev_cmd_t cmd = dev_cmd_make_simple(DEV_CMD_HOME_DEVICE);

    (void)op_mode_handle_command(&cmd);
    op_mode_on_home_done(true);
}

void setUp(void)
{
    wash_ops_stub_reset();
    memset(&s_ops, 0, sizeof(s_ops));
    s_ops.home_device = stub_home_device;
    wash_ops_stub_bind(&s_ops);
    machine_ops_register(&s_ops);
    s_handled_command_id     = 0U;
    s_handled_correlation_id = 0U;
}

void tearDown(void)
{
}

/* IDLE 下 STOP_OPERATION 被接受 */
static void test_submit_accepted_in_idle(void)
{
    dev_cmd_receipt_t receipt = {0};
    pthread_t         tid;

    TEST_ASSERT_EQUAL_INT(SW_OK, event_bus_init());
    TEST_ASSERT_EQUAL_INT(SW_OK, operational_mode_init());
    TEST_ASSERT_EQUAL_INT(SW_OK, command_gateway_init());
    setup_idle();
    tid = start_dispatch();
    usleep(30000);

    TEST_ASSERT_EQUAL_INT(SW_OK, submit_simple(DEV_CMD_STOP_OPERATION, &receipt));
    TEST_ASSERT_EQUAL_INT(DEV_CMD_STATUS_ACCEPTED, receipt.status);

    stop_dispatch(tid);
}

/* 错误模式下命令被拒绝 */
static void test_submit_rejected_wrong_mode(void)
{
    dev_cmd_receipt_t receipt = {0};
    pthread_t         tid;

    TEST_ASSERT_EQUAL_INT(SW_OK, event_bus_init());
    TEST_ASSERT_EQUAL_INT(SW_OK, operational_mode_init());
    TEST_ASSERT_EQUAL_INT(SW_OK, command_gateway_init());
    tid = start_dispatch();

    /* STOPPED 状态下 STOP_WASH 被拒绝 */
    TEST_ASSERT_EQUAL_INT(SW_ERR_STATE, submit_simple(DEV_CMD_STOP_WASH, &receipt));
    TEST_ASSERT_EQUAL_INT(DEV_CMD_STATUS_REJECTED, receipt.status);

    stop_dispatch(tid);
}

/* START_WASH 触发洗车编排器 */
static void test_start_wash_triggers_orchestrator(void)
{
    dev_cmd_t         cmd     = dev_cmd_make_start_wash(TEST_WASH_MODE_A);
    dev_cmd_receipt_t receipt = {0};
    pthread_t         tid;

    TEST_ASSERT_EQUAL_INT(SW_OK, event_bus_init());
    TEST_ASSERT_EQUAL_INT(SW_OK, operational_mode_init());
    TEST_ASSERT_EQUAL_INT(SW_OK, command_gateway_init());
    setup_idle();
    tid = start_dispatch();
    usleep(30000);

    TEST_ASSERT_EQUAL_INT(SW_OK, device_command_port_get_ops()->submit(&cmd, &receipt, 1000U));
    TEST_ASSERT_EQUAL_INT(DEV_CMD_STATUS_ACCEPTED, receipt.status);
    TEST_ASSERT_EQUAL_INT(1, wash_ops_stub_start_count());
    TEST_ASSERT_EQUAL_INT(TEST_WASH_MODE_A, wash_ops_stub_last_mode());

    stop_dispatch(tid);
}

/* STOP_OPERATION → RESUME_OPERATION */
static void test_stop_operation_then_resume(void)
{
    dev_cmd_receipt_t receipt = {0};
    pthread_t         tid;

    TEST_ASSERT_EQUAL_INT(SW_OK, event_bus_init());
    TEST_ASSERT_EQUAL_INT(SW_OK, operational_mode_init());
    TEST_ASSERT_EQUAL_INT(SW_OK, command_gateway_init());
    setup_idle();
    tid = start_dispatch();
    usleep(30000);

    TEST_ASSERT_EQUAL_INT(SW_OK, submit_simple(DEV_CMD_STOP_OPERATION, &receipt));
    TEST_ASSERT_EQUAL_INT(DEV_CMD_STATUS_ACCEPTED, receipt.status);
    TEST_ASSERT_FALSE(op_mode_is_service_enabled());
    TEST_ASSERT_EQUAL_INT(OP_MODE_STOPPED, op_mode_get_current());

    TEST_ASSERT_EQUAL_INT(SW_OK, submit_simple(DEV_CMD_RESUME_OPERATION, &receipt));
    TEST_ASSERT_EQUAL_INT(DEV_CMD_STATUS_ACCEPTED, receipt.status);
    TEST_ASSERT_TRUE(op_mode_is_service_enabled());
    TEST_ASSERT_EQUAL_INT(OP_MODE_STOPPED, op_mode_get_current());

    stop_dispatch(tid);
}

static void test_gateway_assigns_request_id_and_trace(void)
{
    dev_cmd_receipt_t receipt = {0};
    pthread_t         tid;

    TEST_ASSERT_EQUAL_INT(SW_OK, event_bus_init());
    TEST_ASSERT_EQUAL_INT(SW_OK, operational_mode_init());
    TEST_ASSERT_EQUAL_INT(SW_OK, command_gateway_init());
    TEST_ASSERT_EQUAL_INT(SW_OK, event_subscribe(EVT_OP_MODE_CMD_HANDLED, handled_handler));
    setup_idle();
    tid = start_dispatch();
    usleep(30000);

    TEST_ASSERT_EQUAL_INT(SW_OK, submit_simple(DEV_CMD_STOP_OPERATION, &receipt));
    usleep(30000);
    TEST_ASSERT_NOT_EQUAL(0U, receipt.request_id);
    TEST_ASSERT_EQUAL_UINT64(receipt.request_id, s_handled_command_id);
    TEST_ASSERT_EQUAL_UINT64(receipt.request_id, s_handled_correlation_id);

    stop_dispatch(tid);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_submit_accepted_in_idle);
    RUN_TEST(test_submit_rejected_wrong_mode);
    RUN_TEST(test_start_wash_triggers_orchestrator);
    RUN_TEST(test_stop_operation_then_resume);
    RUN_TEST(test_gateway_assigns_request_id_and_trace);
    return UNITY_END();
}
