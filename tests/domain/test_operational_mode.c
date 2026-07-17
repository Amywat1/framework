/**
 * @file    test_operational_mode.c
 * @brief   operational_mode 命令矩阵与模式转移单元测试
 */

#include "common/sw_error.h"
#include "domain/op_mode/device_command.h"
#include "domain/op_mode/op_mode_types.h"
#include "domain/op_mode/operational_mode.h"

#include "domain/safety/alarm_registry/alarm_registry.h"
#include "domain/safety/model/alarm_types.h"
#include "unity.h"

#define TEST_ALARM_BLOCKING ALARM_CODE_MAKE(ALM_C_SENSE, 1U, ALM_N_SIG_ERR)

static const alarm_def_t s_catalog[] = {
    {
     .code             = TEST_ALARM_BLOCKING,
     .level            = ALARM_LEVEL_MAJOR,
     .response         = RESP_COMPLETE_THEN_ASSESS,
     .clear            = ALARM_CLEAR_MANUAL_RESET,
     .source_kind      = ALARM_SOURCE_LEVEL,
     .reeval_group     = ALARM_REEVAL_GROUP_NONE,
     .immediate_cutout = false,
     .desc             = "test blocking",
     },
};

static void load_alarm_catalog(void)
{
    TEST_ASSERT_EQUAL_INT(SW_OK, alarm_registry_load_catalog(s_catalog, 1U));
}

/* 辅助：从 STOPPED 归位到 IDLE */
static void enter_idle(void)
{
    dev_cmd_t          cmd = dev_cmd_make_simple(DEV_CMD_HOME_DEVICE);
    dev_cmd_decision_t d   = op_mode_handle_command(&cmd);

    TEST_ASSERT_EQUAL_INT(OP_CMD_ALLOWED, d.verdict);
    TEST_ASSERT_EQUAL_INT(OP_MODE_HOMING, op_mode_get_current());
    op_mode_on_home_completed(true);
    TEST_ASSERT_EQUAL_INT(OP_MODE_IDLE, op_mode_get_current());
}

void setUp(void)
{
    TEST_ASSERT_EQUAL_INT(SW_OK, operational_mode_init());
}

void tearDown(void)
{
}

/* 上电后应处于 STOPPED，service_enabled=true，非待机 */
static void test_init_stopped_and_service_enabled(void)
{
    TEST_ASSERT_EQUAL_INT(OP_MODE_STOPPED, op_mode_get_current());
    TEST_ASSERT_TRUE(op_mode_is_service_enabled());
    TEST_ASSERT_FALSE(op_mode_is_standby());
    TEST_ASSERT_FALSE(op_mode_is_estop_active());
}

/* STOPPED → HOME_DEVICE → HOMING → IDLE */
static void test_home_device_enters_idle(void)
{
    enter_idle();
    TEST_ASSERT_TRUE(op_mode_is_standby());
}

/* 归位失败 → EXCEPTION */
static void test_home_device_failure_enters_exception(void)
{
    dev_cmd_t          cmd = dev_cmd_make_simple(DEV_CMD_HOME_DEVICE);
    dev_cmd_decision_t d   = op_mode_handle_command(&cmd);

    TEST_ASSERT_EQUAL_INT(OP_CMD_ALLOWED, d.verdict);
    op_mode_on_home_completed(false);
    TEST_ASSERT_EQUAL_INT(OP_MODE_EXCEPTION, op_mode_get_current());
}

/* service_enabled=false 时 HOME_DEVICE 被拒绝 */
static void test_home_device_denied_when_service_disabled(void)
{
    dev_cmd_t cmd;

    op_mode_set_service_enabled(false);
    cmd = dev_cmd_make_simple(DEV_CMD_HOME_DEVICE);
    TEST_ASSERT_EQUAL_INT(OP_REJECT_SERVICE_DISABLED, op_mode_handle_command(&cmd).reason);
}

/* IDLE 可以接单 */
static void test_start_wash_allowed_in_idle(void)
{
    dev_cmd_t          cmd = dev_cmd_make_start_wash(WASH_MODE_STANDARD);
    dev_cmd_decision_t d;

    enter_idle();
    d = op_mode_handle_command(&cmd);
    TEST_ASSERT_EQUAL_INT(OP_CMD_ALLOWED, d.verdict);
    TEST_ASSERT_EQUAL_INT(DEV_CMD_EFFECT_START_WASH, d.pending_effect);
}

/* IDLE 不允许 STOP_WASH */
static void test_stop_wash_denied_in_idle(void)
{
    dev_cmd_t          cmd = dev_cmd_make_simple(DEV_CMD_STOP_WASH);
    dev_cmd_decision_t d;

    enter_idle();
    d = op_mode_handle_command(&cmd);
    TEST_ASSERT_EQUAL_INT(OP_CMD_DENIED, d.verdict);
    TEST_ASSERT_EQUAL_INT(OP_REJECT_WRONG_MODE, d.reason);
}

/* STOP_OPERATION 关闭接单 */
static void test_stop_operation_disables_service(void)
{
    dev_cmd_t cmd = dev_cmd_make_simple(DEV_CMD_STOP_OPERATION);

    enter_idle();
    TEST_ASSERT_EQUAL_INT(OP_CMD_ALLOWED, op_mode_handle_command(&cmd).verdict);
    TEST_ASSERT_FALSE(op_mode_is_service_enabled());
    TEST_ASSERT_TRUE(op_mode_is_stopping());
}

/* RESUME_OPERATION 仅在 service 已停止时有效 */
static void test_resume_operation_only_when_service_stopped(void)
{
    dev_cmd_t stop_cmd   = dev_cmd_make_simple(DEV_CMD_STOP_OPERATION);
    dev_cmd_t resume_cmd = dev_cmd_make_simple(DEV_CMD_RESUME_OPERATION);

    enter_idle();
    TEST_ASSERT_EQUAL_INT(OP_CMD_DENIED, op_mode_handle_command(&resume_cmd).verdict);

    (void)op_mode_handle_command(&stop_cmd);
    TEST_ASSERT_EQUAL_INT(OP_CMD_ALLOWED, op_mode_handle_command(&resume_cmd).verdict);
    TEST_ASSERT_TRUE(op_mode_is_service_enabled());
}

/* service 停止时拒绝 START_WASH */
static void test_start_wash_denied_when_service_disabled(void)
{
    dev_cmd_t stop_cmd = dev_cmd_make_simple(DEV_CMD_STOP_OPERATION);
    dev_cmd_t cmd      = dev_cmd_make_start_wash(WASH_MODE_STANDARD);

    enter_idle();
    (void)op_mode_handle_command(&stop_cmd);
    TEST_ASSERT_EQUAL_INT(OP_REJECT_SERVICE_DISABLED, op_mode_handle_command(&cmd).reason);
}

/* 有阻塞告警时拒绝 START_WASH */
static void test_start_wash_denied_with_blocking_alarm(void)
{
    dev_cmd_t cmd = dev_cmd_make_start_wash(WASH_MODE_STANDARD);

    load_alarm_catalog();
    enter_idle();
    TEST_ASSERT_EQUAL_INT(SW_OK, alarm_registry_trigger(TEST_ALARM_BLOCKING));
    TEST_ASSERT_EQUAL_INT(OP_REJECT_WRONG_MODE, op_mode_handle_command(&cmd).reason);
}

/* 急停激活时 RECOVER 被拒绝 */
static void test_estop_blocks_recover(void)
{
    dev_cmd_t cmd = dev_cmd_make_simple(DEV_CMD_RECOVER);

    op_mode_on_estop_triggered();
    TEST_ASSERT_TRUE(op_mode_is_estop_active());
    TEST_ASSERT_EQUAL_INT(OP_MODE_EXCEPTION, op_mode_get_current());
    TEST_ASSERT_EQUAL_INT(OP_REJECT_ESTOP_ACTIVE, op_mode_handle_command(&cmd).reason);
}

/* 洗车完整生命周期 */
static void test_wash_session_lifecycle(void)
{
    enter_idle();

    /* 正常流程 */
    op_mode_on_wash_session_started();
    TEST_ASSERT_EQUAL_INT(OP_MODE_WASHING, op_mode_get_current());

    op_mode_on_wash_session_completed();
    TEST_ASSERT_EQUAL_INT(OP_MODE_WASH_DONE, op_mode_get_current());

    op_mode_on_wash_customer_gone();
    TEST_ASSERT_EQUAL_INT(OP_MODE_IDLE, op_mode_get_current());

    /* 异常中止流程 */
    op_mode_on_wash_session_started();
    op_mode_on_wash_session_aborted(WASH_ABORT_CRITICAL);
    TEST_ASSERT_EQUAL_INT(OP_MODE_ALARM_HOMING, op_mode_get_current());

    op_mode_on_alarm_home_done();
    TEST_ASSERT_EQUAL_INT(OP_MODE_EXCEPTION, op_mode_get_current());
}

/* STOPPED 允许手动点动 */
static void test_manual_actuator_allowed_in_stopped(void)
{
    dev_cmd_t          cmd = dev_cmd_make_manual(7U, -1);
    dev_cmd_decision_t d   = op_mode_handle_command(&cmd);

    TEST_ASSERT_EQUAL_INT(OP_CMD_ALLOWED, d.verdict);
    TEST_ASSERT_EQUAL_INT(DEV_CMD_EFFECT_MANUAL_ACTUATOR, d.pending_effect);
}

/* EXCEPTION 无急停时允许手动点动 */
static void test_manual_actuator_allowed_in_exception_without_estop(void)
{
    dev_cmd_t cmd;

    op_mode_on_critical_alarm();
    TEST_ASSERT_EQUAL_INT(OP_MODE_EXCEPTION, op_mode_get_current());

    cmd = dev_cmd_make_manual(3U, 0);
    TEST_ASSERT_EQUAL_INT(OP_CMD_ALLOWED, op_mode_handle_command(&cmd).verdict);
}

/* EXCEPTION + 急停时手动点动被拒绝 */
static void test_manual_actuator_denied_in_exception_with_estop(void)
{
    dev_cmd_t cmd;

    op_mode_on_estop_triggered();
    TEST_ASSERT_EQUAL_INT(OP_MODE_EXCEPTION, op_mode_get_current());

    cmd = dev_cmd_make_manual(3U, 0);
    TEST_ASSERT_EQUAL_INT(OP_REJECT_ESTOP_ACTIVE, op_mode_handle_command(&cmd).reason);
}

/* 自检：STOPPED 成功 → 回 STOPPED */
static void test_self_check_from_stopped_success_returns_stopped(void)
{
    dev_cmd_t cmd = dev_cmd_make_simple(DEV_CMD_START_SELF_CHECK);

    TEST_ASSERT_EQUAL_INT(OP_CMD_ALLOWED, op_mode_handle_command(&cmd).verdict);
    TEST_ASSERT_EQUAL_INT(OP_MODE_SELF_CHECK, op_mode_get_current());

    op_mode_on_self_check_completed(false);
    TEST_ASSERT_EQUAL_INT(OP_MODE_STOPPED, op_mode_get_current());
}

/* 自检：STOPPED 失败 → EXCEPTION */
static void test_self_check_from_stopped_fail_enters_exception(void)
{
    dev_cmd_t cmd = dev_cmd_make_simple(DEV_CMD_START_SELF_CHECK);

    (void)op_mode_handle_command(&cmd);
    op_mode_on_self_check_completed(true);
    TEST_ASSERT_EQUAL_INT(OP_MODE_EXCEPTION, op_mode_get_current());
}

/* 自检：从 EXCEPTION 出发，无论结果都回 EXCEPTION */
static void test_self_check_from_exception_always_returns_exception(void)
{
    dev_cmd_t cmd = dev_cmd_make_simple(DEV_CMD_START_SELF_CHECK);

    op_mode_on_critical_alarm();
    TEST_ASSERT_EQUAL_INT(OP_MODE_EXCEPTION, op_mode_get_current());

    (void)op_mode_handle_command(&cmd);
    op_mode_on_self_check_completed(false); /* 成功也回 EXCEPTION */
    TEST_ASSERT_EQUAL_INT(OP_MODE_EXCEPTION, op_mode_get_current());
}

int main(void)
{
    UNITY_BEGIN();

    RUN_TEST(test_init_stopped_and_service_enabled);
    RUN_TEST(test_home_device_enters_idle);
    RUN_TEST(test_home_device_failure_enters_exception);
    RUN_TEST(test_home_device_denied_when_service_disabled);
    RUN_TEST(test_start_wash_allowed_in_idle);
    RUN_TEST(test_stop_wash_denied_in_idle);
    RUN_TEST(test_stop_operation_disables_service);
    RUN_TEST(test_resume_operation_only_when_service_stopped);
    RUN_TEST(test_start_wash_denied_when_service_disabled);
    RUN_TEST(test_start_wash_denied_with_blocking_alarm);
    RUN_TEST(test_estop_blocks_recover);
    RUN_TEST(test_wash_session_lifecycle);
    RUN_TEST(test_manual_actuator_allowed_in_stopped);
    RUN_TEST(test_manual_actuator_allowed_in_exception_without_estop);
    RUN_TEST(test_manual_actuator_denied_in_exception_with_estop);
    RUN_TEST(test_self_check_from_stopped_success_returns_stopped);
    RUN_TEST(test_self_check_from_stopped_fail_enters_exception);
    RUN_TEST(test_self_check_from_exception_always_returns_exception);

    return UNITY_END();
}
