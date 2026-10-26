/**
 * @file    test_command_submit.c
 * @brief   入站命令提交 helper：超时、空端口、追踪编号
 */

#include "application/command_submit.h"
#include "application/ports/inbound/command/command_port.h"
#include "common/log.h"
#include "common/sw_error.h"
#include "common/trace_context.h"
#include "wdf_test_spec.h"

#include <string.h>

static const device_command_port_ops_t *s_ops;
static unsigned                         s_cmd_count;
static dev_cmd_kind_t                   s_kind;
static uint32_t                         s_timeout_ms;
static uint64_t                         s_request_id;
static sw_err_t                         s_sync_ret;
static trace_context_t                  s_bound_trace;
static bool                             s_port_called;

static sw_err_t stub_submit_sync(const dev_cmd_t *cmd, dev_cmd_receipt_t *receipt,
                                 uint32_t timeout_ms)
{
    if (cmd == NULL) {
        return SW_ERR_PARAM;
    }
    s_port_called = true;
    s_cmd_count++;
    s_kind       = cmd->body.kind;
    s_timeout_ms = timeout_ms;
    s_request_id = cmd->meta.request_id;
    s_bound_trace = trace_context_get();
    if (receipt != NULL) {
        memset(receipt, 0, sizeof(*receipt));
        receipt->status       = (s_sync_ret == SW_OK) ? DEV_CMD_STATUS_ACCEPTED
                                                      : DEV_CMD_STATUS_REJECTED;
        receipt->effect_error = s_sync_ret;
        receipt->request_id   = cmd->meta.request_id;
    }
    return s_sync_ret;
}

static const device_command_port_ops_t s_port = {
    .submit_async = NULL,
    .submit_sync  = stub_submit_sync,
};

const device_command_port_ops_t *device_command_port_get_ops(void)
{
    return s_ops;
}

void sw_log_write(sw_log_level_t level, const char *source, const char *fmt, ...)
{
    (void)level;
    (void)source;
    (void)fmt;
}

const char *sw_log_source_file_name(const char *path)
{
    return path;
}

void setUp(void)
{
    s_ops         = &s_port;
    s_cmd_count   = 0U;
    s_kind        = DEV_CMD_NONE;
    s_timeout_ms  = 0U;
    s_request_id  = 0U;
    s_sync_ret    = SW_OK;
    s_port_called = false;
    memset(&s_bound_trace, 0, sizeof(s_bound_trace));
    trace_context_set(NULL);
}

void tearDown(void)
{
}

static void test_null_cmd_is_param(void)
{
    TEST_ASSERT_EQUAL_INT(SW_ERR_PARAM, command_submit(NULL));
    TEST_ASSERT_EQUAL_INT(SW_ERR_PARAM, command_submit_dispatch(NULL, NULL));
    TEST_ASSERT_FALSE(s_port_called);
}

static void test_unregistered_port_is_not_init(void)
{
    dev_cmd_t cmd = dev_cmd_make_simple(DEV_CMD_STOP_ALL_OUTPUTS);
    dev_cmd_receipt_t receipt;

    s_ops = NULL;
    memset(&receipt, 0, sizeof(receipt));
    TEST_ASSERT_EQUAL_INT(SW_ERR_NOT_INIT, command_submit_dispatch(&cmd, &receipt));
    TEST_ASSERT_EQUAL_INT(DEV_CMD_STATUS_FAILED, receipt.status);
    TEST_ASSERT_EQUAL_INT(SW_ERR_NOT_INIT, receipt.effect_error);
    TEST_ASSERT_FALSE(s_port_called);
}

static void test_dispatch_uses_default_timeout(void)
{
    dev_cmd_t cmd = dev_cmd_make_simple(DEV_CMD_RECOVER);

    TEST_ASSERT_EQUAL_INT(SW_OK, command_submit_dispatch(&cmd, NULL));
    TEST_ASSERT_EQUAL_UINT32(COMMAND_SUBMIT_TIMEOUT_MS, s_timeout_ms);
}

static void test_prepare_assigns_request_id(void)
{
    dev_cmd_t       cmd = dev_cmd_make_start_wash();
    trace_context_t previous;

    command_submit_prepare(&cmd, &previous);
    TEST_ASSERT_NOT_EQUAL_UINT64(0U, cmd.meta.request_id);
    TEST_ASSERT_EQUAL_UINT64(cmd.meta.request_id, cmd.meta.wash_session_id);
    TEST_ASSERT_EQUAL_UINT64(cmd.meta.request_id, trace_context_get().command_id);
    trace_context_set(&previous);
}

static void test_prepare_keeps_existing_request_id(void)
{
    dev_cmd_t       cmd = dev_cmd_make_simple(DEV_CMD_RECOVER);
    trace_context_t previous;

    cmd.meta.request_id = 42U;
    command_submit_prepare(&cmd, &previous);
    TEST_ASSERT_EQUAL_UINT64(42U, cmd.meta.request_id);
    trace_context_set(&previous);
}

static void test_submit_restores_trace(void)
{
    trace_context_t before = {
        .wash_session_id = 7U,
        .command_id      = 8U,
        .correlation_id  = 9U,
        .causation_id    = 10U,
    };
    trace_context_t after;

    trace_context_set(&before);
    TEST_ASSERT_EQUAL_INT(SW_OK, command_submit_kind(DEV_CMD_RECOVER, DEV_CMD_SOURCE_CLI));
    after = trace_context_get();
    TEST_ASSERT_EQUAL_UINT64(before.command_id, after.command_id);
    TEST_ASSERT_EQUAL_UINT64(before.correlation_id, after.correlation_id);
    TEST_ASSERT_EQUAL_INT(DEV_CMD_RECOVER, s_kind);
    TEST_ASSERT_NOT_EQUAL_UINT64(0U, s_request_id);
    TEST_ASSERT_EQUAL_UINT64(s_request_id, s_bound_trace.command_id);
}

static void test_manual_and_service_reach_port(void)
{
    TEST_ASSERT_EQUAL_INT(SW_OK, command_submit_manual(3U, -1, DEV_CMD_SOURCE_TEST));
    TEST_ASSERT_EQUAL_INT(DEV_CMD_MANUAL_ACTUATOR, s_kind);
    TEST_ASSERT_EQUAL_INT(SW_OK, command_submit_set_service(true, DEV_CMD_SOURCE_CLOUD));
    TEST_ASSERT_EQUAL_INT(DEV_CMD_SET_SERVICE, s_kind);
}

int main(void)
{
    UNITY_BEGIN();
    WDF_RUN_TEST(test_null_cmd_is_param, "CMD-08",
                 "空命令返回参数错误且不碰端口");
    WDF_RUN_TEST(test_unregistered_port_is_not_init, "CMD-08",
                 "端口未注册返回未初始化");
    WDF_RUN_TEST(test_dispatch_uses_default_timeout, "CMD-09",
                 "同步提交使用默认 5000 ms 超时");
    WDF_RUN_TEST(test_prepare_assigns_request_id, "CMD-09",
                 "request_id 为 0 时分配非 0 并写入追踪");
    WDF_RUN_TEST(test_prepare_keeps_existing_request_id, "CMD-09",
                 "已有 request_id 不改写");
    WDF_RUN_TEST(test_submit_restores_trace, "CMD-09",
                 "提交结束后恢复调用前追踪");
    WDF_RUN_TEST(test_manual_and_service_reach_port, "CMD-08",
                 "点动与总开关封装到达端口");
    return UNITY_END();
}
