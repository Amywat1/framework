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
#include "wdf_test_spec.h"

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
    dev_cmd_t cmd = dev_cmd_make_simple(DEV_CMD_RECOVER);

    (void)op_mode_handle_command(&cmd);
    op_mode_on_recovery_completed(RECOVERY_RESULT_IDLE);
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

/* 超时后槽位被复用，不得读到上一任槽主的占位 receipt。
 * 覆盖信号量残留计数缺陷：先制造一次超时（不启 dispatch），
 * 再启 dispatch 并重新提交，第二次必须拿到真实裁决结果。 */
static void test_timeout_then_reuse_gets_real_verdict(void)
{
    dev_cmd_receipt_t receipt = {0};
    pthread_t         tid;

    TEST_ASSERT_EQUAL_INT(SW_OK, event_bus_init());
    TEST_ASSERT_EQUAL_INT(SW_OK, operational_mode_init());
    TEST_ASSERT_EQUAL_INT(SW_OK, command_gateway_init());
    setup_idle();

    /* dispatch 未启动 → wake 事件无人消费 → 必然超时 */
    {
        dev_cmd_t cmd = dev_cmd_make_simple(DEV_CMD_STOP_OPERATION);

        TEST_ASSERT_EQUAL_INT(SW_ERR_TIMEOUT, device_command_port_get_ops()->submit(&cmd, &receipt, 50U));
        TEST_ASSERT_EQUAL_INT(DEV_CMD_STATUS_TIMEOUT, receipt.status);
    }

    /* 现在启动 dispatch：它会先消费掉那条积压的 wake 事件 */
    tid = start_dispatch();
    usleep(30000);

    /* 复用同一槽位重新提交，必须得到真实裁决而非残留占位值 */
    memset(&receipt, 0, sizeof(receipt));
    TEST_ASSERT_EQUAL_INT(SW_OK, submit_simple(DEV_CMD_STOP_OPERATION, &receipt));
    TEST_ASSERT_EQUAL_INT(DEV_CMD_STATUS_ACCEPTED, receipt.status);
    TEST_ASSERT_NOT_EQUAL(SW_ERR_BUSY, receipt.effect_error);

    stop_dispatch(tid);
}

/* 从 dispatch 线程内部调用 submit 必须被立即拦截，而不是白等到超时 */
static volatile sw_err_t s_reentrant_ret;
static volatile int      s_reentrant_done;

static void reentrant_submit_handler(const event_t *evt)
{
    dev_cmd_t         cmd = dev_cmd_make_simple(DEV_CMD_STOP_OPERATION);
    dev_cmd_receipt_t receipt;

    (void)evt;
    memset(&receipt, 0, sizeof(receipt));
    s_reentrant_ret  = device_command_port_get_ops()->submit(&cmd, &receipt, 1000U);
    s_reentrant_done = 1;
}

static void test_submit_from_dispatch_thread_is_rejected(void)
{
    dev_cmd_receipt_t receipt = {0};
    pthread_t         tid;

    s_reentrant_ret  = SW_OK;
    s_reentrant_done = 0;

    TEST_ASSERT_EQUAL_INT(SW_OK, event_bus_init());
    TEST_ASSERT_EQUAL_INT(SW_OK, operational_mode_init());
    TEST_ASSERT_EQUAL_INT(SW_OK, command_gateway_init());
    setup_idle();
    tid = start_dispatch();
    usleep(30000);

    /* 先走一次正常提交，让网关记录下 drain 线程身份 */
    TEST_ASSERT_EQUAL_INT(SW_OK, submit_simple(DEV_CMD_STOP_OPERATION, &receipt));

    /* 订阅一个在 dispatch 线程上下文执行的 handler，在其中调 submit */
    TEST_ASSERT_EQUAL_INT(SW_OK, event_subscribe(EVT_OP_MODE_CONTEXT_SYNC, reentrant_submit_handler));
    TEST_ASSERT_EQUAL_INT(SW_OK, event_publish(EVT_OP_MODE_CONTEXT_SYNC, 0U));

    /* 若无防护，此处要等满 1000ms 才返回；有防护则立即返回 */
    usleep(200000);
    TEST_ASSERT_EQUAL_INT(1, s_reentrant_done);
    TEST_ASSERT_EQUAL_INT(SW_ERR_STATE, s_reentrant_ret);

    stop_dispatch(tid);
}

int main(void)
{
    UNITY_BEGIN();
    WDF_RUN_TEST(test_submit_accepted_in_idle, "", "验证空闲模式接受命令提交");
    WDF_RUN_TEST(test_submit_rejected_wrong_mode, "", "验证模式不匹配时拒绝命令提交");
    WDF_RUN_TEST(test_start_wash_triggers_orchestrator, "", "验证启动洗车触发流程编排器");
    WDF_RUN_TEST(test_stop_operation_then_resume, "", "验证停止运行随后恢复运行");
    WDF_RUN_TEST(test_gateway_assigns_request_id_and_trace, "", "验证网关分配请求ID并追踪上下文");
    WDF_RUN_TEST(test_timeout_then_reuse_gets_real_verdict, "", "验证超时随后复用获得真实判定结果");
    WDF_RUN_TEST(test_submit_from_dispatch_thread_is_rejected, "", "验证从分发线程提交命令时被拒绝");
    return UNITY_END();
}
