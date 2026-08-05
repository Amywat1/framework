/**
 * @file    test_event_param_codec.c
 * @brief   事件 param 位段编解码单元测试
 * @author  HUWANGWEI
 * @date    2026-08-03
 *
 * @note    这些用例锁定 param 的位布局契约：发布方与订阅方分处不同模块，
 *          若某一侧改了字段顺序或宽度而另一侧没改，编译器不会报错，只会在
 *          运行期取到错位的值。往返测试让这类改动在 CI 阶段就失败。
 */

#include "domain/op_mode/command_types.h"
#include "domain/op_mode/device_command.h"
#include "domain/op_mode/op_mode_types.h"
#include "wdf_test_spec.h"

void setUp(void)
{
}

void tearDown(void)
{
}

/* EVT_OP_MODE_CHANGED：from/to 往返一致，且互不串位 */
static void test_mode_changed_roundtrip(void)
{
    uint32_t param = op_mode_changed_evt_param(OP_MODE_WASHING, OP_MODE_EXCEPTION);

    TEST_ASSERT_EQUAL_INT(OP_MODE_WASHING, op_mode_changed_from(param));
    TEST_ASSERT_EQUAL_INT(OP_MODE_EXCEPTION, op_mode_changed_to(param));

    /* 相邻模式值不应因移位错误而互相污染 */
    param = op_mode_changed_evt_param(OP_MODE_INIT, OP_MODE_RECOVERING);
    TEST_ASSERT_EQUAL_INT(OP_MODE_INIT, op_mode_changed_from(param));
    TEST_ASSERT_EQUAL_INT(OP_MODE_RECOVERING, op_mode_changed_to(param));
}

/* EVT_OP_MODE_CMD_REJECTED：kind/reason 往返一致 */
static void test_cmd_rejected_roundtrip(void)
{
    uint32_t param = op_mode_cmd_rejected_evt_param((uint8_t)DEV_CMD_START_WASH, OP_REJECT_ESTOP_ACTIVE);

    TEST_ASSERT_EQUAL_UINT8((uint8_t)DEV_CMD_START_WASH, op_mode_cmd_rejected_kind(param));
    TEST_ASSERT_EQUAL_INT(OP_REJECT_ESTOP_ACTIVE, op_mode_cmd_rejected_reason(param));
}

/* EVT_OP_MODE_CMD_HANDLED：三字段往返一致 */
static void test_cmd_handled_roundtrip(void)
{
    uint32_t param
        = cmd_handled_evt_param((uint8_t)DEV_CMD_MANUAL_ACTUATOR, DEV_CMD_STATUS_REJECTED, OP_REJECT_WRONG_MODE);

    TEST_ASSERT_EQUAL_UINT8((uint8_t)DEV_CMD_MANUAL_ACTUATOR, cmd_handled_kind(param));
    TEST_ASSERT_EQUAL_INT(DEV_CMD_STATUS_REJECTED, cmd_handled_status(param));
    TEST_ASSERT_EQUAL_INT(OP_REJECT_WRONG_MODE, cmd_handled_reason(param));
}

/* 三字段各自取满 8 位时仍互不干扰，确认掩码到位 */
static void test_cmd_handled_field_isolation(void)
{
    uint32_t param = cmd_handled_evt_param(0xFFU, (dev_cmd_status_t)0xFFU, (op_reject_reason_t)0xFFU);

    TEST_ASSERT_EQUAL_UINT8(0xFFU, cmd_handled_kind(param));
    TEST_ASSERT_EQUAL_INT(0xFF, (int)cmd_handled_status(param));
    TEST_ASSERT_EQUAL_INT(0xFF, (int)cmd_handled_reason(param));

    /* 只设中间字段时，另两个字段必须为 0 */
    param = cmd_handled_evt_param(0U, (dev_cmd_status_t)0xFFU, (op_reject_reason_t)0U);
    TEST_ASSERT_EQUAL_UINT8(0U, cmd_handled_kind(param));
    TEST_ASSERT_EQUAL_INT(0xFF, (int)cmd_handled_status(param));
    TEST_ASSERT_EQUAL_INT(0, (int)cmd_handled_reason(param));
}

/* EVT_WASH_SESSION_STARTED：wash_mode 往返一致 */
static void test_wash_session_started_roundtrip(void)
{
    TEST_ASSERT_EQUAL_UINT8(3U, wash_session_started_mode(wash_session_started_evt_param((wash_mode_t)3U)));
    TEST_ASSERT_EQUAL_UINT8(0U, wash_session_started_mode(wash_session_started_evt_param((wash_mode_t)0U)));
}

/* EVT_WASH_ABORTED：既有 helper 的往返与越界兜底 */
static void test_wash_abort_roundtrip(void)
{
    TEST_ASSERT_EQUAL_INT(WASH_ABORT_ESTOP, wash_abort_from_evt_param(wash_abort_evt_param(WASH_ABORT_ESTOP)));
    TEST_ASSERT_EQUAL_INT(WASH_ABORT_MANUAL, wash_abort_from_evt_param(wash_abort_evt_param(WASH_ABORT_MANUAL)));

    /* 超出枚举范围的 param 落到 INTERNAL，而不是造出非法枚举值 */
    TEST_ASSERT_EQUAL_INT(WASH_ABORT_INTERNAL, wash_abort_from_evt_param(0xFFFFFFFFU));
}

int main(void)
{
    UNITY_BEGIN();
    WDF_RUN_TEST(test_mode_changed_roundtrip, "", "验证模式变化往返编解码");
    WDF_RUN_TEST(test_cmd_rejected_roundtrip, "", "验证命令被拒绝往返编解码");
    WDF_RUN_TEST(test_cmd_handled_roundtrip, "", "验证命令已处理往返编解码");
    WDF_RUN_TEST(test_cmd_handled_field_isolation, "", "验证命令已处理字段隔离");
    WDF_RUN_TEST(test_wash_session_started_roundtrip, "", "验证洗车会话已启动往返编解码");
    WDF_RUN_TEST(test_wash_abort_roundtrip, "", "验证洗车中止往返编解码");
    return UNITY_END();
}
