/**
 * @file    test_side_effect_router.c
 * @brief   side_effect_router 副作用路由单元测试
 */

#include "application/side_effect_router.h"
#include "common/sw_error.h"
#include "common/time_util.h"
#include "domain/op_mode/device_command.h"
#include "domain/op_mode/op_mode_types.h"
#include "domain/op_mode/operational_mode.h"
#include "domain/ports/outbound/machine/machine_ops_port.h"
#include "domain/safety/alarm_registry/alarm_registry.h"
#include "domain/safety/model/alarm_types.h"
#include "runtime/event_bus/event_bus.h"
#include "tests/stubs/wash_ops_stub.h"
#include "wdf_test_spec.h"

#include <stdint.h>
#include <string.h>

static int      s_manual_count;
static int      s_stop_outputs_count;
static uint32_t s_last_act_id;
static int32_t  s_last_act_param;

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

static machine_ops_t s_ops;

static void reset_counters(void)
{
    s_manual_count       = 0;
    s_stop_outputs_count = 0;
    s_last_act_id        = 0U;
    s_last_act_param     = 0;
    wash_ops_stub_reset();
}

void setUp(void)
{
    reset_counters();
    time_util_init();
    TEST_ASSERT_EQUAL_INT(SW_OK, event_bus_init());
    TEST_ASSERT_EQUAL_INT(SW_OK, operational_mode_init());

    memset(&s_ops, 0, sizeof(s_ops));
    s_ops.execute_manual_actuator = stub_manual_actuator;
    s_ops.stop_all_outputs        = stub_stop_all_outputs;
    wash_ops_stub_bind(&s_ops);
    machine_ops_register(&s_ops);
}

void tearDown(void)
{
    (void)event_bus_shutdown();
}

static void test_recover_is_noop(void)
{
    dev_cmd_t cmd = dev_cmd_make_simple(DEV_CMD_RECOVER);

    TEST_ASSERT_EQUAL_INT(SW_OK, side_effect_router_run(&cmd, OP_MODE_STOPPED));
}

static void test_start_wash_calls_orchestrator(void)
{
    dev_cmd_t cmd = dev_cmd_make_start_wash(TEST_WASH_MODE_B);

    TEST_ASSERT_EQUAL_INT(SW_OK, side_effect_router_run(&cmd, OP_MODE_IDLE));
    TEST_ASSERT_EQUAL_INT(1, wash_ops_stub_start_count());
    TEST_ASSERT_EQUAL_INT(TEST_WASH_MODE_B, wash_ops_stub_last_mode());
}

static void test_stop_wash_aborts_orchestrator(void)
{
    dev_cmd_t cmd = dev_cmd_make_simple(DEV_CMD_STOP_WASH);

    TEST_ASSERT_EQUAL_INT(SW_OK, side_effect_router_run(&cmd, OP_MODE_WASHING));
    TEST_ASSERT_EQUAL_INT(1, wash_ops_stub_abort_count());
    TEST_ASSERT_EQUAL_INT(WASH_ABORT_MANUAL, wash_ops_stub_last_abort_cause());
}

static void test_manual_actuator_forwards_params(void)
{
    dev_cmd_t cmd = dev_cmd_make_manual(42U, -3);

    TEST_ASSERT_EQUAL_INT(SW_OK, side_effect_router_run(&cmd, OP_MODE_STOPPED));
    TEST_ASSERT_EQUAL_INT(1, s_manual_count);
    TEST_ASSERT_EQUAL_UINT32(42U, s_last_act_id);
    TEST_ASSERT_EQUAL_INT32(-3, s_last_act_param);
}

static void test_stop_all_outputs_only_cuts(void)
{
    dev_cmd_t cmd = dev_cmd_make_simple(DEV_CMD_STOP_ALL_OUTPUTS);

    TEST_ASSERT_EQUAL_INT(SW_OK, side_effect_router_run(&cmd, OP_MODE_IDLE));
    TEST_ASSERT_EQUAL_INT(1, s_stop_outputs_count);
    TEST_ASSERT_EQUAL_INT(0, wash_ops_stub_abort_count());
}

static void test_stop_all_outputs_and_abort(void)
{
    dev_cmd_t cmd = dev_cmd_make_simple(DEV_CMD_STOP_ALL_OUTPUTS);

    TEST_ASSERT_EQUAL_INT(SW_OK, side_effect_router_run(&cmd, OP_MODE_WASHING));
    TEST_ASSERT_EQUAL_INT(1, s_stop_outputs_count);
    TEST_ASSERT_EQUAL_INT(1, wash_ops_stub_abort_count());
    TEST_ASSERT_EQUAL_INT(WASH_ABORT_STOP_ALL, wash_ops_stub_last_abort_cause());
}

static void test_machine_ops_not_init_returns_error(void)
{
    dev_cmd_t cmd = dev_cmd_make_simple(DEV_CMD_STOP_ALL_OUTPUTS);

    machine_ops_register(NULL);
    TEST_ASSERT_EQUAL_INT(SW_ERR_NOT_INIT, side_effect_router_run(&cmd, OP_MODE_IDLE));
}

static void test_invalid_kind_returns_param(void)
{
    dev_cmd_t cmd = dev_cmd_make_simple(DEV_CMD_NONE);

    cmd.body.kind = DEV_CMD_MAX;
    TEST_ASSERT_EQUAL_INT(SW_ERR_PARAM, side_effect_router_run(&cmd, OP_MODE_STOPPED));
}

int main(void)
{
    UNITY_BEGIN();

    WDF_RUN_TEST(test_recover_is_noop, "", "验证恢复命令无同步副作用");
    WDF_RUN_TEST(test_start_wash_calls_orchestrator, "", "验证启动洗车调用流程编排器");
    WDF_RUN_TEST(test_stop_wash_aborts_orchestrator, "", "验证停止洗车中止流程编排器");
    WDF_RUN_TEST(test_manual_actuator_forwards_params, "", "验证手动执行器转发参数");
    WDF_RUN_TEST(test_stop_all_outputs_only_cuts, "", "验证全停仅切断输出");
    WDF_RUN_TEST(test_stop_all_outputs_and_abort, "", "验证全停切断并中止洗车会话");
    WDF_RUN_TEST(test_machine_ops_not_init_returns_error, "", "验证设备操作接口未初始化时返回错误");
    WDF_RUN_TEST(test_invalid_kind_returns_param, "", "验证非法命令种类返回参数错误");

    return UNITY_END();
}
