/**
 * @file    test_command_gateway.c
 * @brief   command_gateway 命令网关单元测试
 */

#include "application/command_gateway.h"
#include "application/ports/inbound/command/command_port.h"
#include "common/sw_error.h"
#include "common/trace_context.h"
#include "domain/op_mode/command_types.h"
#include "domain/op_mode/device_command.h"
#include "domain/op_mode/operational_mode.h"
#include "domain/ports/outbound/device/device_ops_port.h"
#include "runtime/event_bus/event_bus.h"
#include "tests/stubs/device_ops_stub.h"
#include "wdf_test_spec.h"

#include <pthread.h>
#include <string.h>
#include <unistd.h>

static volatile uint64_t s_handled_command_id;
static volatile uint64_t s_handled_correlation_id;
static volatile int      s_stop_all_outputs_count;
static volatile int      s_cmd_handled_busy_count;

static sw_err_t stub_home_device(void)
{
    return SW_OK;
}

static sw_err_t stub_stop_all_outputs(void)
{
    s_stop_all_outputs_count++;
    return SW_OK;
}

static device_ops_t s_ops;

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
    if (cmd_handled_status(evt->param) == DEV_CMD_STATUS_BUSY) {
        s_cmd_handled_busy_count++;
    }
}

static sw_err_t submit_simple(dev_cmd_kind_t kind, dev_cmd_receipt_t *receipt)
{
    dev_cmd_t cmd = dev_cmd_make_simple(kind);

    return device_command_port_get_ops()->submit_sync(&cmd, receipt, 1000U);
}

static void setup_idle(void)
{
    dev_cmd_t cmd = dev_cmd_make_simple(DEV_CMD_RECOVER);

    (void)op_mode_handle_command(&cmd);
    op_mode_on_recovery_completed(RECOVERY_RESULT_IDLE);
}

static void start_runtime(pthread_t *dispatch_tid)
{
    TEST_ASSERT_EQUAL_INT(SW_OK, command_gateway_start_control_for_test());
    *dispatch_tid = start_dispatch();
    usleep(30000);
}

static void stop_runtime(pthread_t dispatch_tid)
{
    command_gateway_stop_control_for_test();
    stop_dispatch(dispatch_tid);
}

void setUp(void)
{
    device_ops_stub_reset();
    memset(&s_ops, 0, sizeof(s_ops));
    s_ops.home_device      = stub_home_device;
    s_ops.stop_all_outputs = stub_stop_all_outputs;
    device_ops_stub_bind(&s_ops);
    device_ops_register(&s_ops);
    s_handled_command_id     = 0U;
    s_handled_correlation_id = 0U;
    s_stop_all_outputs_count = 0;
    s_cmd_handled_busy_count = 0;
}

void tearDown(void)
{
    command_gateway_stop_control_for_test();
}

static void test_submit_accepted_in_idle(void)
{
    dev_cmd_receipt_t receipt = {0};
    pthread_t         tid;

    TEST_ASSERT_EQUAL_INT(SW_OK, event_bus_init());
    TEST_ASSERT_EQUAL_INT(SW_OK, operational_mode_init());
    TEST_ASSERT_EQUAL_INT(SW_OK, command_gateway_init());
    setup_idle();
    start_runtime(&tid);

    TEST_ASSERT_EQUAL_INT(SW_OK, submit_simple(DEV_CMD_CLOUD_SYNC, &receipt));
    TEST_ASSERT_EQUAL_INT(DEV_CMD_STATUS_ACCEPTED, receipt.status);

    stop_runtime(tid);
}

static void test_submit_rejected_wrong_mode(void)
{
    dev_cmd_receipt_t receipt = {0};
    pthread_t         tid;

    TEST_ASSERT_EQUAL_INT(SW_OK, event_bus_init());
    TEST_ASSERT_EQUAL_INT(SW_OK, operational_mode_init());
    TEST_ASSERT_EQUAL_INT(SW_OK, command_gateway_init());
    start_runtime(&tid);

    TEST_ASSERT_EQUAL_INT(SW_ERR_STATE, submit_simple(DEV_CMD_STOP_WASH, &receipt));
    TEST_ASSERT_EQUAL_INT(DEV_CMD_STATUS_REJECTED, receipt.status);

    stop_runtime(tid);
}

static void test_start_wash_triggers_orchestrator(void)
{
    dev_cmd_t         cmd     = dev_cmd_make_start_wash(TEST_WASH_MODE_A);
    dev_cmd_receipt_t receipt = {0};
    pthread_t         tid;

    TEST_ASSERT_EQUAL_INT(SW_OK, event_bus_init());
    TEST_ASSERT_EQUAL_INT(SW_OK, operational_mode_init());
    TEST_ASSERT_EQUAL_INT(SW_OK, command_gateway_init());
    setup_idle();
    start_runtime(&tid);

    TEST_ASSERT_EQUAL_INT(SW_OK, device_command_port_get_ops()->submit_sync(&cmd, &receipt, 1000U));
    TEST_ASSERT_EQUAL_INT(DEV_CMD_STATUS_ACCEPTED, receipt.status);
    TEST_ASSERT_EQUAL_INT(1, device_ops_stub_start_count());
    TEST_ASSERT_EQUAL_INT(TEST_WASH_MODE_A, device_ops_stub_last_mode());

    stop_runtime(tid);
}

static void test_stop_all_then_set_service(void)
{
    dev_cmd_receipt_t receipt = {0};
    pthread_t         tid;
    dev_cmd_t         off_cmd;
    dev_cmd_t         on_cmd;

    TEST_ASSERT_EQUAL_INT(SW_OK, event_bus_init());
    TEST_ASSERT_EQUAL_INT(SW_OK, operational_mode_init());
    TEST_ASSERT_EQUAL_INT(SW_OK, command_gateway_init());
    setup_idle();
    start_runtime(&tid);

    TEST_ASSERT_EQUAL_INT(SW_OK, submit_simple(DEV_CMD_STOP_ALL_OUTPUTS, &receipt));
    TEST_ASSERT_EQUAL_INT(DEV_CMD_STATUS_ACCEPTED, receipt.status);
    TEST_ASSERT_TRUE(op_mode_is_service_enabled());
    TEST_ASSERT_EQUAL_INT(OP_MODE_STOPPED, op_mode_get_current());

    off_cmd = dev_cmd_make_set_service(false);
    TEST_ASSERT_EQUAL_INT(SW_OK, device_command_port_get_ops()->submit_sync(&off_cmd, &receipt, 1000U));
    TEST_ASSERT_EQUAL_INT(DEV_CMD_STATUS_ACCEPTED, receipt.status);
    TEST_ASSERT_FALSE(op_mode_is_service_enabled());
    TEST_ASSERT_EQUAL_INT(OP_MODE_STOPPED, op_mode_get_current());

    on_cmd = dev_cmd_make_set_service(true);
    TEST_ASSERT_EQUAL_INT(SW_OK, device_command_port_get_ops()->submit_sync(&on_cmd, &receipt, 1000U));
    TEST_ASSERT_EQUAL_INT(DEV_CMD_STATUS_ACCEPTED, receipt.status);
    TEST_ASSERT_TRUE(op_mode_is_service_enabled());
    TEST_ASSERT_EQUAL_INT(OP_MODE_STOPPED, op_mode_get_current());

    stop_runtime(tid);
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
    start_runtime(&tid);

    TEST_ASSERT_EQUAL_INT(SW_OK, submit_simple(DEV_CMD_CLOUD_SYNC, &receipt));
    usleep(30000);
    TEST_ASSERT_NOT_EQUAL(0U, receipt.request_id);
    TEST_ASSERT_EQUAL_UINT64(receipt.request_id, s_handled_command_id);
    TEST_ASSERT_EQUAL_UINT64(receipt.request_id, s_handled_correlation_id);

    stop_runtime(tid);
}

static void test_timeout_then_reuse_gets_real_verdict(void)
{
    dev_cmd_receipt_t receipt = {0};
    pthread_t         tid;

    TEST_ASSERT_EQUAL_INT(SW_OK, event_bus_init());
    TEST_ASSERT_EQUAL_INT(SW_OK, operational_mode_init());
    TEST_ASSERT_EQUAL_INT(SW_OK, command_gateway_init());
    setup_idle();

    {
        dev_cmd_t cmd = dev_cmd_make_simple(DEV_CMD_CLOUD_SYNC);

        TEST_ASSERT_EQUAL_INT(SW_ERR_TIMEOUT, device_command_port_get_ops()->submit_sync(&cmd, &receipt, 50U));
        TEST_ASSERT_EQUAL_INT(DEV_CMD_STATUS_TIMEOUT, receipt.status);
    }

    start_runtime(&tid);

    memset(&receipt, 0, sizeof(receipt));
    TEST_ASSERT_EQUAL_INT(SW_OK, submit_simple(DEV_CMD_CLOUD_SYNC, &receipt));
    TEST_ASSERT_EQUAL_INT(DEV_CMD_STATUS_ACCEPTED, receipt.status);
    TEST_ASSERT_NOT_EQUAL(SW_ERR_BUSY, receipt.effect_error);

    stop_runtime(tid);
}

/* 从 cmd_control 线程内 submit_sync 必须立即拒绝 */
static volatile sw_err_t s_reentrant_ret;
static volatile int      s_reentrant_done;

static sw_err_t stub_stop_all_reentrant(void)
{
    dev_cmd_t         cmd = dev_cmd_make_simple(DEV_CMD_CLOUD_SYNC);
    dev_cmd_receipt_t receipt;

    s_stop_all_outputs_count++;
    memset(&receipt, 0, sizeof(receipt));
    s_reentrant_ret  = device_command_port_get_ops()->submit_sync(&cmd, &receipt, 1000U);
    s_reentrant_done = 1;
    return SW_OK;
}

static void test_submit_sync_from_control_thread_is_rejected(void)
{
    dev_cmd_t         wash_cmd = dev_cmd_make_start_wash(TEST_WASH_MODE_A);
    dev_cmd_receipt_t receipt  = {0};
    pthread_t         tid;

    s_reentrant_ret        = SW_OK;
    s_reentrant_done       = 0;
    s_ops.stop_all_outputs = stub_stop_all_reentrant;
    device_ops_register(&s_ops);

    TEST_ASSERT_EQUAL_INT(SW_OK, event_bus_init());
    TEST_ASSERT_EQUAL_INT(SW_OK, operational_mode_init());
    TEST_ASSERT_EQUAL_INT(SW_OK, command_gateway_init());
    setup_idle();
    start_runtime(&tid);

    TEST_ASSERT_EQUAL_INT(SW_OK, device_command_port_get_ops()->submit_sync(&wash_cmd, &receipt, 1000U));
    op_mode_on_wash_session_started();
    TEST_ASSERT_EQUAL_INT(SW_OK, submit_simple(DEV_CMD_STOP_ALL_OUTPUTS, &receipt));
    TEST_ASSERT_EQUAL_INT(1, s_reentrant_done);
    TEST_ASSERT_EQUAL_INT(SW_ERR_STATE, s_reentrant_ret);

    stop_runtime(tid);
}

static void test_stop_all_during_washing_via_gateway(void)
{
    dev_cmd_t         wash_cmd = dev_cmd_make_start_wash(TEST_WASH_MODE_A);
    dev_cmd_receipt_t receipt  = {0};
    pthread_t         tid;

    TEST_ASSERT_EQUAL_INT(SW_OK, event_bus_init());
    TEST_ASSERT_EQUAL_INT(SW_OK, operational_mode_init());
    TEST_ASSERT_EQUAL_INT(SW_OK, command_gateway_init());
    setup_idle();
    start_runtime(&tid);

    TEST_ASSERT_EQUAL_INT(SW_OK, device_command_port_get_ops()->submit_sync(&wash_cmd, &receipt, 1000U));
    TEST_ASSERT_EQUAL_INT(DEV_CMD_STATUS_ACCEPTED, receipt.status);
    op_mode_on_wash_session_started();
    TEST_ASSERT_EQUAL_INT(OP_MODE_WASHING, op_mode_get_current());

    TEST_ASSERT_EQUAL_INT(SW_OK, submit_simple(DEV_CMD_STOP_ALL_OUTPUTS, &receipt));
    TEST_ASSERT_EQUAL_INT(DEV_CMD_STATUS_ACCEPTED, receipt.status);
    TEST_ASSERT_EQUAL_INT(OP_MODE_STOPPED, op_mode_get_current());
    TEST_ASSERT_EQUAL_INT(1, s_stop_all_outputs_count);
    TEST_ASSERT_EQUAL_INT(1, device_ops_stub_abort_count());
    TEST_ASSERT_EQUAL_INT(WASH_ABORT_STOP_ALL, device_ops_stub_last_abort_cause());

    stop_runtime(tid);
}

static void test_submit_async_completes_via_event(void)
{
    dev_cmd_t cmd        = dev_cmd_make_simple(DEV_CMD_STOP_ALL_OUTPUTS);
    uint64_t  request_id = 0U;
    pthread_t tid;
    int       spins;

    s_handled_command_id = 0U;

    TEST_ASSERT_EQUAL_INT(SW_OK, event_bus_init());
    TEST_ASSERT_EQUAL_INT(SW_OK, operational_mode_init());
    TEST_ASSERT_EQUAL_INT(SW_OK, command_gateway_init());
    TEST_ASSERT_EQUAL_INT(SW_OK, event_subscribe(EVT_OP_MODE_CMD_HANDLED, handled_handler));
    setup_idle();
    start_runtime(&tid);

    TEST_ASSERT_EQUAL_INT(SW_OK, device_command_port_get_ops()->submit_async(&cmd, &request_id));
    TEST_ASSERT_NOT_EQUAL(0U, request_id);

    for (spins = 0; (s_handled_command_id == 0U) && (spins < 100); spins++) {
        usleep(1000);
    }
    TEST_ASSERT_EQUAL_UINT64(request_id, s_handled_command_id);
    TEST_ASSERT_EQUAL_INT(OP_MODE_STOPPED, op_mode_get_current());
    TEST_ASSERT_TRUE(op_mode_is_service_enabled());

    stop_runtime(tid);
}

/*
 * sync 挂接正在执行的 async STOP：副作用进行中 waiting 由 false→true，
 * drain 结束须重读 waiting 并 sem_post，否则 sync 会误超时。
 */
static sw_err_t stub_stop_all_slow(void)
{
    usleep(200000U);
    s_stop_all_outputs_count++;
    return SW_OK;
}

static void test_sync_stop_attaches_to_in_flight_async(void)
{
    dev_cmd_t         async_cmd = dev_cmd_make_simple(DEV_CMD_STOP_ALL_OUTPUTS);
    dev_cmd_t         sync_cmd  = dev_cmd_make_simple(DEV_CMD_STOP_ALL_OUTPUTS);
    dev_cmd_receipt_t receipt   = {0};
    uint64_t          rid       = 0U;
    pthread_t         tid;

    s_ops.stop_all_outputs = stub_stop_all_slow;
    device_ops_register(&s_ops);

    TEST_ASSERT_EQUAL_INT(SW_OK, event_bus_init());
    TEST_ASSERT_EQUAL_INT(SW_OK, operational_mode_init());
    TEST_ASSERT_EQUAL_INT(SW_OK, command_gateway_init());
    setup_idle();
    start_runtime(&tid);

    TEST_ASSERT_EQUAL_INT(SW_OK, device_command_port_get_ops()->submit_async(&async_cmd, &rid));
    usleep(20000U); /* 让 control 进入慢 stop_all，再挂接 sync */
    TEST_ASSERT_EQUAL_INT(SW_OK, device_command_port_get_ops()->submit_sync(&sync_cmd, &receipt, 1000U));
    TEST_ASSERT_EQUAL_INT(DEV_CMD_STATUS_ACCEPTED, receipt.status);
    TEST_ASSERT_EQUAL_INT(1, s_stop_all_outputs_count);

    stop_runtime(tid);
}

/*
 * 队列被普通 async 命令占满时，STOP_ALL 抢占一槽并优先执行。
 * 不启动 control 先塞满，再提交 STOP，再启动 control。
 */
static void test_stop_all_preempts_full_queue(void)
{
    uint64_t  rid;
    pthread_t tid;
    int       spins;

    TEST_ASSERT_EQUAL_INT(SW_OK, event_bus_init());
    TEST_ASSERT_EQUAL_INT(SW_OK, operational_mode_init());
    TEST_ASSERT_EQUAL_INT(SW_OK, command_gateway_init());
    TEST_ASSERT_EQUAL_INT(SW_OK, event_subscribe(EVT_OP_MODE_CMD_HANDLED, handled_handler));
    setup_idle();

    for (int i = 0; i < 4; i++) {
        dev_cmd_t cmd = dev_cmd_make_simple(DEV_CMD_CLOUD_SYNC);

        TEST_ASSERT_EQUAL_INT(SW_OK, device_command_port_get_ops()->submit_async(&cmd, &rid));
    }

    {
        dev_cmd_t stop = dev_cmd_make_simple(DEV_CMD_STOP_ALL_OUTPUTS);

        TEST_ASSERT_EQUAL_INT(SW_OK, device_command_port_get_ops()->submit_async(&stop, &rid));
    }

    start_runtime(&tid);

    for (spins = 0; (s_stop_all_outputs_count < 1) && (spins < 200); spins++) {
        usleep(1000);
    }
    TEST_ASSERT_EQUAL_INT(1, s_stop_all_outputs_count);
    TEST_ASSERT_TRUE(s_cmd_handled_busy_count >= 1);
    TEST_ASSERT_EQUAL_INT(OP_MODE_STOPPED, op_mode_get_current());

    stop_runtime(tid);
}

int main(void)
{
    UNITY_BEGIN();
    WDF_RUN_TEST(test_submit_accepted_in_idle, "", "验证空闲模式接受命令提交");
    WDF_RUN_TEST(test_submit_rejected_wrong_mode, "", "验证模式不匹配时拒绝命令提交");
    WDF_RUN_TEST(test_start_wash_triggers_orchestrator, "", "验证启动洗车触发流程编排器");
    WDF_RUN_TEST(test_stop_all_during_washing_via_gateway, "", "验证洗车中经网关全停切断并中止");
    WDF_RUN_TEST(test_stop_all_then_set_service, "", "验证全停后独立开关总开关");
    WDF_RUN_TEST(test_gateway_assigns_request_id_and_trace, "", "验证网关分配请求ID并追踪上下文");
    WDF_RUN_TEST(test_timeout_then_reuse_gets_real_verdict, "", "验证超时随后复用获得真实判定结果");
    WDF_RUN_TEST(test_submit_sync_from_control_thread_is_rejected, "", "验证控制线程内同步提交被拒绝");
    WDF_RUN_TEST(test_submit_async_completes_via_event, "", "验证异步提交经事件完成并带回请求ID");
    WDF_RUN_TEST(test_sync_stop_attaches_to_in_flight_async, "", "验证同步全停挂接飞行中异步全停");
    WDF_RUN_TEST(test_stop_all_preempts_full_queue, "", "验证队列满时全停抢占并优先执行");
    return UNITY_END();
}
