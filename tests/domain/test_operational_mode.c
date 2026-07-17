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

void setUp(void)
{
    TEST_ASSERT_EQUAL_INT(SW_OK, operational_mode_init());
}

void tearDown(void)
{
}

static void test_init_idle_and_service_enabled(void)
{
    TEST_ASSERT_EQUAL_INT(OP_MODE_IDLE, op_mode_get_current());
    TEST_ASSERT_TRUE(op_mode_is_service_enabled());
    TEST_ASSERT_TRUE(op_mode_is_standby());
    TEST_ASSERT_FALSE(op_mode_is_estop_active());
}

static void test_enter_manual_from_idle(void)
{
    dev_cmd_t          cmd = dev_cmd_make_simple(DEV_CMD_ENTER_MANUAL);
    dev_cmd_decision_t d   = op_mode_handle_command(&cmd);

    TEST_ASSERT_EQUAL_INT(OP_CMD_ALLOWED, d.verdict);
    TEST_ASSERT_EQUAL_INT(DEV_CMD_EFFECT_NONE, d.pending_effect);
    TEST_ASSERT_EQUAL_INT(OP_MODE_MANUAL, op_mode_get_current());
}

static void test_start_wash_allowed_in_idle(void)
{
    dev_cmd_t          cmd = dev_cmd_make_start_wash(WASH_MODE_STANDARD);
    dev_cmd_decision_t d   = op_mode_handle_command(&cmd);

    TEST_ASSERT_EQUAL_INT(OP_CMD_ALLOWED, d.verdict);
    TEST_ASSERT_EQUAL_INT(DEV_CMD_EFFECT_START_WASH, d.pending_effect);
}

static void test_stop_wash_denied_in_idle(void)
{
    dev_cmd_t          cmd = dev_cmd_make_simple(DEV_CMD_STOP_WASH);
    dev_cmd_decision_t d   = op_mode_handle_command(&cmd);

    TEST_ASSERT_EQUAL_INT(OP_CMD_DENIED, d.verdict);
    TEST_ASSERT_EQUAL_INT(OP_REJECT_WRONG_MODE, d.reason);
}

static void test_stop_operation_disables_service(void)
{
    dev_cmd_t cmd = dev_cmd_make_simple(DEV_CMD_STOP_OPERATION);

    TEST_ASSERT_EQUAL_INT(OP_CMD_ALLOWED, op_mode_handle_command(&cmd).verdict);
    TEST_ASSERT_FALSE(op_mode_is_service_enabled());
    TEST_ASSERT_TRUE(op_mode_is_stopping());
}

static void test_resume_operation_only_when_stopped(void)
{
    dev_cmd_t stop_cmd   = dev_cmd_make_simple(DEV_CMD_STOP_OPERATION);
    dev_cmd_t resume_cmd = dev_cmd_make_simple(DEV_CMD_RESUME_OPERATION);

    TEST_ASSERT_EQUAL_INT(OP_CMD_DENIED, op_mode_handle_command(&resume_cmd).verdict);

    (void)op_mode_handle_command(&stop_cmd);
    TEST_ASSERT_EQUAL_INT(OP_CMD_ALLOWED, op_mode_handle_command(&resume_cmd).verdict);
    TEST_ASSERT_TRUE(op_mode_is_service_enabled());
}

static void test_start_wash_denied_when_service_disabled(void)
{
    dev_cmd_t stop_cmd = dev_cmd_make_simple(DEV_CMD_STOP_OPERATION);
    dev_cmd_t cmd      = dev_cmd_make_start_wash(WASH_MODE_STANDARD);

    (void)op_mode_handle_command(&stop_cmd);
    TEST_ASSERT_EQUAL_INT(OP_REJECT_SERVICE_DISABLED, op_mode_handle_command(&cmd).reason);
}

static void test_start_wash_denied_with_blocking_alarm(void)
{
    dev_cmd_t cmd = dev_cmd_make_start_wash(WASH_MODE_STANDARD);

    load_alarm_catalog();
    TEST_ASSERT_EQUAL_INT(SW_OK, alarm_registry_trigger(TEST_ALARM_BLOCKING));
    TEST_ASSERT_EQUAL_INT(OP_REJECT_WRONG_MODE, op_mode_handle_command(&cmd).reason);
}

static void test_estop_blocks_conditional_reset_fault(void)
{
    dev_cmd_t cmd = dev_cmd_make_simple(DEV_CMD_RESET_FAULT);

    op_mode_on_estop_triggered();
    TEST_ASSERT_TRUE(op_mode_is_estop_active());
    TEST_ASSERT_EQUAL_INT(OP_MODE_EXCEPTION, op_mode_get_current());

    TEST_ASSERT_EQUAL_INT(OP_REJECT_ESTOP_ACTIVE, op_mode_handle_command(&cmd).reason);
}

static void test_wash_session_lifecycle(void)
{
    op_mode_on_wash_session_started();
    TEST_ASSERT_EQUAL_INT(OP_MODE_WASHING, op_mode_get_current());

    op_mode_on_wash_session_completed();
    TEST_ASSERT_EQUAL_INT(OP_MODE_IDLE, op_mode_get_current());

    op_mode_on_wash_session_started();
    op_mode_on_wash_session_aborted(WASH_ABORT_CRITICAL);
    TEST_ASSERT_EQUAL_INT(OP_MODE_EXCEPTION, op_mode_get_current());
}

static void test_manual_actuator_allowed_in_manual_mode(void)
{
    dev_cmd_t          enter = dev_cmd_make_simple(DEV_CMD_ENTER_MANUAL);
    dev_cmd_t          cmd   = dev_cmd_make_manual(7U, -1);
    dev_cmd_decision_t d;

    (void)op_mode_handle_command(&enter);
    d = op_mode_handle_command(&cmd);

    TEST_ASSERT_EQUAL_INT(OP_CMD_ALLOWED, d.verdict);
    TEST_ASSERT_EQUAL_INT(DEV_CMD_EFFECT_MANUAL_ACTUATOR, d.pending_effect);
}

int main(void)
{
    UNITY_BEGIN();

    RUN_TEST(test_init_idle_and_service_enabled);
    RUN_TEST(test_enter_manual_from_idle);
    RUN_TEST(test_start_wash_allowed_in_idle);
    RUN_TEST(test_stop_wash_denied_in_idle);
    RUN_TEST(test_stop_operation_disables_service);
    RUN_TEST(test_resume_operation_only_when_stopped);
    RUN_TEST(test_start_wash_denied_when_service_disabled);
    RUN_TEST(test_start_wash_denied_with_blocking_alarm);
    RUN_TEST(test_estop_blocks_conditional_reset_fault);
    RUN_TEST(test_wash_session_lifecycle);
    RUN_TEST(test_manual_actuator_allowed_in_manual_mode);

    return UNITY_END();
}
