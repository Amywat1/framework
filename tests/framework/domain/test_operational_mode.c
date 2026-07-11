/**
 * @file    test_operational_mode.c
 * @brief   OperationalMode 聚合单元测试
 * @author  HUWANGWEI
 * @date    2026-07-09
 */

#include "framework/domain/command_gateway/operational_mode.h"
#include "framework/domain/safety/alarm_registry/alarm_registry.h"
#include "framework/domain/safety/model/alarm_types.h"
#include "framework/domain/command_gateway/op_mode_types.h"
#include "framework/ports/inbound/command/command_port.h"
#include "framework/runtime/event_bus/event_bus.h"
#include "framework/common/time_util.h"
#include "unity.h"

void setUp(void)
{
    (void)time_util_init();
    (void)event_bus_init();
    (void)alarm_registry_init();
    (void)operational_mode_init();
}

void tearDown(void)
{
    (void)event_bus_shutdown();
}

static void test_enter_manual_from_idle(void)
{
    cmd_t cmd = { .type = CMD_ENTER_MANUAL };
    op_command_result_t r = op_mode_handle_command(&cmd);

    TEST_ASSERT_EQUAL_INT(OP_CMD_ALLOWED, (int)r.result);
    TEST_ASSERT_EQUAL_INT(OP_MODE_MANUAL, (int)op_mode_get_current());
}

static void test_start_wash_denied_in_manual(void)
{
    cmd_t enter = { .type = CMD_ENTER_MANUAL };
    (void)op_mode_handle_command(&enter);

    cmd_t start = { .type = CMD_START_WASH };
    start.payload.start_wash.mode = WASH_MODE_STANDARD;
    op_command_result_t r = op_mode_handle_command(&start);

    TEST_ASSERT_EQUAL_INT(OP_CMD_DENIED, (int)r.result);
}

static void test_exception_allows_self_check_when_estop_clear(void)
{
    op_mode_on_critical_alarm();
    TEST_ASSERT_EQUAL_INT(OP_MODE_EXCEPTION, (int)op_mode_get_current());

    cmd_t sc = { .type = CMD_START_SELF_CHECK };
    op_command_result_t r = op_mode_handle_command(&sc);

    TEST_ASSERT_EQUAL_INT(OP_CMD_ALLOWED, (int)r.result);
    TEST_ASSERT_EQUAL_INT(OP_MODE_SELF_CHECK, (int)op_mode_get_current());
}

static void test_estop_blocks_recover(void)
{
    op_mode_on_estop_triggered();

    cmd_t rec = { .type = CMD_RECOVER };
    op_command_result_t r = op_mode_handle_command(&rec);

    TEST_ASSERT_EQUAL_INT(OP_CMD_DENIED, (int)r.result);
    TEST_ASSERT_TRUE(op_mode_is_estop_active());
}

static void test_wash_abort_manual_returns_idle(void)
{
    op_mode_on_wash_session_started();
    TEST_ASSERT_EQUAL_INT(OP_MODE_WASHING, (int)op_mode_get_current());

    op_mode_on_wash_session_aborted(WASH_ABORT_MANUAL);
    TEST_ASSERT_EQUAL_INT(OP_MODE_IDLE, (int)op_mode_get_current());
}

static void test_stop_operation_keeps_idle_mode(void)
{
    cmd_t stop = { .type = CMD_STOP_OPERATION };
    op_command_result_t r = op_mode_handle_command(&stop);

    TEST_ASSERT_EQUAL_INT(OP_CMD_ALLOWED, (int)r.result);
    TEST_ASSERT_EQUAL_INT(OP_MODE_IDLE, (int)op_mode_get_current());
    TEST_ASSERT_FALSE(op_mode_is_service_enabled());
}

static void test_manual_actuator_allowed_in_manual(void)
{
    cmd_t enter = { .type = CMD_ENTER_MANUAL };
    cmd_t act = { .type = CMD_MANUAL_ACTUATOR };
    op_command_result_t r;

    (void)op_mode_handle_command(&enter);
    r = op_mode_handle_command(&act);
    TEST_ASSERT_EQUAL_INT(OP_CMD_ALLOWED, (int)r.result);
}

static void test_manual_actuator_denied_in_idle(void)
{
    cmd_t act = { .type = CMD_MANUAL_ACTUATOR };
    op_command_result_t r = op_mode_handle_command(&act);

    TEST_ASSERT_EQUAL_INT(OP_CMD_DENIED, (int)r.result);
}

static void test_home_device_allowed_in_exception(void)
{
    cmd_t home = { .type = CMD_HOME_DEVICE };
    op_command_result_t r;

    op_mode_on_critical_alarm();
    r = op_mode_handle_command(&home);
    TEST_ASSERT_EQUAL_INT(OP_CMD_ALLOWED, (int)r.result);
}

static void test_wash_abort_internal_enters_exception(void)
{
    op_mode_on_wash_session_started();
    op_mode_on_wash_session_aborted(WASH_ABORT_INTERNAL);
    TEST_ASSERT_EQUAL_INT(OP_MODE_EXCEPTION, (int)op_mode_get_current());
}

static void test_wash_abort_timeout_enters_exception(void)
{
    op_mode_on_wash_session_started();
    op_mode_on_wash_session_aborted(WASH_ABORT_STEP_TIMEOUT);
    TEST_ASSERT_EQUAL_INT(OP_MODE_EXCEPTION, (int)op_mode_get_current());
}

static void test_wash_abort_critical_enters_exception(void)
{
    op_mode_on_wash_session_started();
    op_mode_on_wash_session_aborted(WASH_ABORT_CRITICAL);
    TEST_ASSERT_EQUAL_INT(OP_MODE_EXCEPTION, (int)op_mode_get_current());
}

static void test_post_wash_blocking_enters_exception(void)
{
    static const alarm_def_t cat[] = {
        {
            .code = 201101U, .level = ALARM_LEVEL_MAJOR,
            .response = RESP_COMPLETE_THEN_ASSESS,
            .clear = ALARM_CLEAR_MANUAL_RESET,
            .source_kind = ALARM_SOURCE_LEVEL,
            .reeval_group = ALARM_REEVAL_GROUP_NONE,
            .immediate_cutout = false,
            .desc = "侧刷过载",
        },
    };

    (void)alarm_registry_load_catalog(cat, 1U);
    op_mode_on_wash_session_started();
    (void)alarm_registry_trigger(201101U);
    op_mode_on_wash_session_completed();
    alarm_registry_on_wash_session_ended();
    op_mode_on_post_wash_assessment(alarm_registry_has_blocking_active());
    TEST_ASSERT_EQUAL_INT(OP_MODE_EXCEPTION, (int)op_mode_get_current());
}

static void test_resume_operation_restores_service(void)
{
    cmd_t stop = { .type = CMD_STOP_OPERATION };
    cmd_t resume = { .type = CMD_RESUME_OPERATION };
    op_command_result_t r;

    (void)op_mode_handle_command(&stop);
    TEST_ASSERT_FALSE(op_mode_is_service_enabled());

    r = op_mode_handle_command(&resume);
    TEST_ASSERT_EQUAL_INT(OP_CMD_ALLOWED, (int)r.result);
    TEST_ASSERT_TRUE(op_mode_is_service_enabled());
}

static void test_is_stopping_and_standby(void)
{
    TEST_ASSERT_TRUE(op_mode_is_standby());
    TEST_ASSERT_FALSE(op_mode_is_stopping());

    cmd_t stop = { .type = CMD_STOP_OPERATION };
    (void)op_mode_handle_command(&stop);
    TEST_ASSERT_FALSE(op_mode_is_standby());
    TEST_ASSERT_TRUE(op_mode_is_stopping());

    cmd_t resume = { .type = CMD_RESUME_OPERATION };
    (void)op_mode_handle_command(&resume);
    TEST_ASSERT_TRUE(op_mode_is_standby());
    TEST_ASSERT_FALSE(op_mode_is_stopping());

    op_mode_on_critical_alarm();
    TEST_ASSERT_FALSE(op_mode_is_standby());
    TEST_ASSERT_TRUE(op_mode_is_stopping());
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_enter_manual_from_idle);
    RUN_TEST(test_start_wash_denied_in_manual);
    RUN_TEST(test_exception_allows_self_check_when_estop_clear);
    RUN_TEST(test_estop_blocks_recover);
    RUN_TEST(test_wash_abort_manual_returns_idle);
    RUN_TEST(test_stop_operation_keeps_idle_mode);
    RUN_TEST(test_manual_actuator_allowed_in_manual);
    RUN_TEST(test_manual_actuator_denied_in_idle);
    RUN_TEST(test_home_device_allowed_in_exception);
    RUN_TEST(test_wash_abort_internal_enters_exception);
    RUN_TEST(test_wash_abort_timeout_enters_exception);
    RUN_TEST(test_wash_abort_critical_enters_exception);
    RUN_TEST(test_post_wash_blocking_enters_exception);
    RUN_TEST(test_resume_operation_restores_service);
    RUN_TEST(test_is_stopping_and_standby);
    return UNITY_END();
}
