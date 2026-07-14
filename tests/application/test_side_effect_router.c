/**
 * @file    test_side_effect_router.c
 * @brief   side_effect_router 副作用路由单元测试
 */

#include "application/side_effect_router.h"
#include "common/sw_error.h"
#include "domain/command_gateway/device_command.h"
#include "domain/command_gateway/operational_mode.h"
#include "domain/safety/alarm_registry/alarm_registry.h"
#include "domain/safety/model/alarm_types.h"
#include "ports/outbound/machine/machine_ops_port.h"
#include "tests/stubs/wash_orchestrator_stub.h"
#include "unity.h"

#include <stdint.h>

#define TEST_ALARM_MANUAL ALARM_CODE_MAKE(ALM_C_SENSE, 2U, ALM_N_SIG_ERR)

static int      s_home_count;
static int      s_manual_count;
static int      s_stop_outputs_count;
static uint32_t s_last_act_id;
static int32_t  s_last_act_param;

static sw_err_t stub_home_device(void)
{
    s_home_count++;
    return SW_OK;
}

static sw_err_t stub_manual_actuator(uint32_t act_id, int32_t param)
{
    s_manual_count++;
    s_last_act_id    = act_id;
    s_last_act_param = param;
    return SW_OK;
}

static sw_err_t stub_stop_all_outputs(void)
{
    s_stop_outputs_count++;
    return SW_OK;
}

static const machine_ops_t s_ops = {
    .home_device             = stub_home_device,
    .execute_manual_actuator = stub_manual_actuator,
    .stop_all_outputs        = stub_stop_all_outputs,
};

static const alarm_def_t s_catalog[] = {
    {
     .code             = TEST_ALARM_MANUAL,
     .level            = ALARM_LEVEL_MAJOR,
     .response         = RESP_COMPLETE_THEN_ASSESS,
     .clear            = ALARM_CLEAR_MANUAL_RESET,
     .source_kind      = ALARM_SOURCE_LEVEL,
     .reeval_group     = ALARM_REEVAL_GROUP_NONE,
     .immediate_cutout = false,
     .desc             = "manual reset alarm",
     },
};

static void reset_counters(void)
{
    s_home_count         = 0;
    s_manual_count       = 0;
    s_stop_outputs_count = 0;
    s_last_act_id        = 0U;
    s_last_act_param     = 0;
    wash_orchestrator_stub_reset();
}

void setUp(void)
{
    reset_counters();
    TEST_ASSERT_EQUAL_INT(SW_OK, operational_mode_init());
    machine_ops_register(&s_ops);
}

void tearDown(void)
{
}

static void test_effect_none_returns_ok(void)
{
    dev_cmd_t cmd = dev_cmd_make_simple(DEV_CMD_ENTER_MANUAL);

    TEST_ASSERT_EQUAL_INT(SW_OK, side_effect_router_run(DEV_CMD_EFFECT_NONE, &cmd));
}

static void test_start_wash_calls_orchestrator(void)
{
    dev_cmd_t cmd = dev_cmd_make_start_wash(WASH_MODE_QUICK);

    TEST_ASSERT_EQUAL_INT(SW_OK, side_effect_router_run(DEV_CMD_EFFECT_START_WASH, &cmd));
    TEST_ASSERT_EQUAL_INT(1, wash_orchestrator_stub_start_count());
    TEST_ASSERT_EQUAL_INT(WASH_MODE_QUICK, wash_orchestrator_stub_last_mode());
}

static void test_stop_wash_aborts_orchestrator(void)
{
    dev_cmd_t cmd = dev_cmd_make_simple(DEV_CMD_STOP_WASH);

    TEST_ASSERT_EQUAL_INT(SW_OK, side_effect_router_run(DEV_CMD_EFFECT_STOP_WASH, &cmd));
    TEST_ASSERT_EQUAL_INT(1, wash_orchestrator_stub_abort_count());
    TEST_ASSERT_EQUAL_INT(WASH_ABORT_MANUAL, wash_orchestrator_stub_last_abort_cause());
}

static void test_home_device_calls_machine_ops(void)
{
    dev_cmd_t cmd = dev_cmd_make_simple(DEV_CMD_HOME_DEVICE);

    TEST_ASSERT_EQUAL_INT(SW_OK, side_effect_router_run(DEV_CMD_EFFECT_HOME_DEVICE, &cmd));
    TEST_ASSERT_EQUAL_INT(1, s_home_count);
}

static void test_manual_actuator_forwards_params(void)
{
    dev_cmd_t cmd = dev_cmd_make_manual(42U, -3);

    TEST_ASSERT_EQUAL_INT(SW_OK, side_effect_router_run(DEV_CMD_EFFECT_MANUAL_ACTUATOR, &cmd));
    TEST_ASSERT_EQUAL_INT(1, s_manual_count);
    TEST_ASSERT_EQUAL_UINT32(42U, s_last_act_id);
    TEST_ASSERT_EQUAL_INT32(-3, s_last_act_param);
}

static void test_stop_all_outputs_calls_machine_ops(void)
{
    dev_cmd_t cmd = dev_cmd_make_simple(DEV_CMD_STOP_ALL_OUTPUTS);

    TEST_ASSERT_EQUAL_INT(SW_OK, side_effect_router_run(DEV_CMD_EFFECT_STOP_ALL_OUTPUTS, &cmd));
    TEST_ASSERT_EQUAL_INT(1, s_stop_outputs_count);
}

static void test_reset_fault_in_idle_without_alarm_rejected(void)
{
    dev_cmd_t cmd = dev_cmd_make_simple(DEV_CMD_RESET_FAULT);

    TEST_ASSERT_EQUAL_INT(SW_ERR_STATE, side_effect_router_run(DEV_CMD_EFFECT_RESET_FAULT, &cmd));
}

static void test_reset_fault_in_exception_clears_and_transitions(void)
{
    dev_cmd_t cmd = dev_cmd_make_simple(DEV_CMD_RESET_FAULT);

    TEST_ASSERT_EQUAL_INT(SW_OK, alarm_registry_load_catalog(s_catalog, 1U));
    TEST_ASSERT_EQUAL_INT(SW_OK, alarm_registry_trigger(TEST_ALARM_MANUAL));
    op_mode_on_wash_session_started();
    op_mode_on_wash_session_aborted(WASH_ABORT_CRITICAL);
    TEST_ASSERT_EQUAL_INT(OP_MODE_EXCEPTION, op_mode_get_current());

    TEST_ASSERT_EQUAL_INT(SW_OK, side_effect_router_run(DEV_CMD_EFFECT_RESET_FAULT, &cmd));
    TEST_ASSERT_FALSE(alarm_registry_is_active(TEST_ALARM_MANUAL));
    TEST_ASSERT_EQUAL_INT(OP_MODE_IDLE, op_mode_get_current());
}

static void test_machine_ops_not_init_returns_error(void)
{
    dev_cmd_t cmd = dev_cmd_make_simple(DEV_CMD_HOME_DEVICE);

    machine_ops_register(NULL);
    TEST_ASSERT_EQUAL_INT(SW_ERR_NOT_INIT, side_effect_router_run(DEV_CMD_EFFECT_HOME_DEVICE, &cmd));
}

int main(void)
{
    UNITY_BEGIN();

    RUN_TEST(test_effect_none_returns_ok);
    RUN_TEST(test_start_wash_calls_orchestrator);
    RUN_TEST(test_stop_wash_aborts_orchestrator);
    RUN_TEST(test_home_device_calls_machine_ops);
    RUN_TEST(test_manual_actuator_forwards_params);
    RUN_TEST(test_stop_all_outputs_calls_machine_ops);
    RUN_TEST(test_reset_fault_in_idle_without_alarm_rejected);
    RUN_TEST(test_reset_fault_in_exception_clears_and_transitions);
    RUN_TEST(test_machine_ops_not_init_returns_error);

    return UNITY_END();
}
