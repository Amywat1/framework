/**
 * @file    test_op_mode_bridge.c
 * @brief   op_mode_bridge 运行模式事件桥接单测
 */

#include "application/bridges/op_mode_bridge.h"
#include "common/event_types.h"
#include "common/sw_error.h"
#include "common/time_util.h"
#include "domain/op_mode/device_command.h"
#include "domain/op_mode/op_mode_types.h"
#include "domain/op_mode/operational_mode.h"
#include "domain/safety/alarm_registry/alarm_registry.h"
#include "domain/safety/model/alarm_types.h"
#include "runtime/event_bus/event_bus.h"
#include "wdf_test_spec.h"



static void publish_and_wait(event_type_t type, uint32_t param)
{
    TEST_ASSERT_EQUAL_INT(SW_OK, event_publish(type, param));
    TEST_ASSERT_EQUAL_INT(SW_OK, event_bus_drain());
}

/* 辅助：直接将 op_mode 推进到 IDLE（绕过 event_bus）*/
static void setup_idle(void)
{
    dev_cmd_t cmd = dev_cmd_make_simple(DEV_CMD_RECOVER);

    (void)op_mode_handle_command(&cmd); /* STOPPED → RECOVERING */
    op_mode_on_recovery_completed(RECOVERY_RESULT_IDLE);
}

void setUp(void)
{
}

void tearDown(void)
{
}

/* 急停触发 → STOPPED */
static void test_hw_estop_on_enters_stopped(void)
{
    time_util_init();
    TEST_ASSERT_EQUAL_INT(SW_OK, event_bus_init());
    TEST_ASSERT_EQUAL_INT(SW_OK, operational_mode_init());
    TEST_ASSERT_EQUAL_INT(SW_OK, op_mode_bridge_init());
    publish_and_wait(EVT_HW_ESTOP_ON, 0U);
    TEST_ASSERT_TRUE(op_mode_is_estop_active());
    TEST_ASSERT_EQUAL_INT(OP_MODE_STOPPED, op_mode_get_current());

}

/* EVT_WASH_SESSION_STARTED → WASHING */
static void test_wash_session_started_enters_washing(void)
{
    time_util_init();
    TEST_ASSERT_EQUAL_INT(SW_OK, event_bus_init());
    TEST_ASSERT_EQUAL_INT(SW_OK, operational_mode_init());
    TEST_ASSERT_EQUAL_INT(SW_OK, op_mode_bridge_init());
    setup_idle(); /* STOPPED → IDLE */
    publish_and_wait(EVT_WASH_SESSION_STARTED, 0U);
    TEST_ASSERT_EQUAL_INT(OP_MODE_WASHING, op_mode_get_current());

}

/* 正常洗车完成 → WASH_DONE */
static void test_wash_done_enters_wash_done(void)
{
    time_util_init();
    TEST_ASSERT_EQUAL_INT(SW_OK, event_bus_init());
    TEST_ASSERT_EQUAL_INT(SW_OK, operational_mode_init());
    TEST_ASSERT_EQUAL_INT(SW_OK, op_mode_bridge_init());
    setup_idle();
    publish_and_wait(EVT_WASH_SESSION_STARTED, 0U);
    publish_and_wait(EVT_WASH_DONE, 0U);
    TEST_ASSERT_EQUAL_INT(OP_MODE_WASH_DONE, op_mode_get_current());

}

/* 洗车完成 + 客户离场 → IDLE */
static void test_customer_gone_returns_idle(void)
{
    time_util_init();
    TEST_ASSERT_EQUAL_INT(SW_OK, event_bus_init());
    TEST_ASSERT_EQUAL_INT(SW_OK, operational_mode_init());
    TEST_ASSERT_EQUAL_INT(SW_OK, op_mode_bridge_init());
    setup_idle();
    publish_and_wait(EVT_WASH_SESSION_STARTED, 0U);
    publish_and_wait(EVT_WASH_DONE, 0U);
    publish_and_wait(EVT_WASH_CUSTOMER_GONE, 0U);
    TEST_ASSERT_EQUAL_INT(OP_MODE_IDLE, op_mode_get_current());

}

/* 手动停止洗车 → ABORT_HOMING */
static void test_wash_aborted_manual_enters_alarm_homing(void)
{
    time_util_init();
    TEST_ASSERT_EQUAL_INT(SW_OK, event_bus_init());
    TEST_ASSERT_EQUAL_INT(SW_OK, operational_mode_init());
    TEST_ASSERT_EQUAL_INT(SW_OK, op_mode_bridge_init());
    setup_idle();
    publish_and_wait(EVT_WASH_SESSION_STARTED, 0U);
    publish_and_wait(EVT_WASH_ABORTED, wash_abort_evt_param(WASH_ABORT_MANUAL));
    TEST_ASSERT_EQUAL_INT(OP_MODE_ABORT_HOMING, op_mode_get_current());

}

/* 自检完成（从 STOPPED 出发，成功）→ STOPPED */
static void test_self_check_from_stopped_lands_stopped(void)
{
    dev_cmd_t cmd = dev_cmd_make_simple(DEV_CMD_START_SELF_CHECK);
    time_util_init();
    TEST_ASSERT_EQUAL_INT(SW_OK, event_bus_init());
    TEST_ASSERT_EQUAL_INT(SW_OK, operational_mode_init());
    TEST_ASSERT_EQUAL_INT(SW_OK, op_mode_bridge_init());
    (void)op_mode_handle_command(&cmd); /* STOPPED → SELF_CHECK */
    publish_and_wait(EVT_OP_MODE_SELF_CHECK_COMPLETED, 0U); /* land_exception=false */
    TEST_ASSERT_EQUAL_INT(OP_MODE_STOPPED, op_mode_get_current());

}

/* 自检进行中急停 → 立即 STOPPED（不等自检完成事件） */
static void test_estop_during_self_check_enters_stopped(void)
{
    dev_cmd_t cmd = dev_cmd_make_simple(DEV_CMD_START_SELF_CHECK);
    time_util_init();
    TEST_ASSERT_EQUAL_INT(SW_OK, event_bus_init());
    TEST_ASSERT_EQUAL_INT(SW_OK, operational_mode_init());
    TEST_ASSERT_EQUAL_INT(SW_OK, op_mode_bridge_init());
    TEST_ASSERT_EQUAL_INT(OP_CMD_ALLOWED, op_mode_handle_command(&cmd).verdict);
    TEST_ASSERT_EQUAL_INT(OP_MODE_SELF_CHECK, op_mode_get_current());

    publish_and_wait(EVT_HW_ESTOP_ON, 0U);

    TEST_ASSERT_TRUE(op_mode_is_estop_active());
    TEST_ASSERT_EQUAL_INT(OP_MODE_STOPPED, op_mode_get_current());

}

/* EVT_OP_MODE_RECOVERY_COMPLETED → RECOVERING → IDLE */
static void test_recovery_completed_success_enters_idle(void)
{
    time_util_init();
    TEST_ASSERT_EQUAL_INT(SW_OK, event_bus_init());
    TEST_ASSERT_EQUAL_INT(SW_OK, operational_mode_init());
    TEST_ASSERT_EQUAL_INT(SW_OK, op_mode_bridge_init());

    {
        dev_cmd_t cmd = dev_cmd_make_simple(DEV_CMD_RECOVER);
        (void)op_mode_handle_command(&cmd);
    }
    TEST_ASSERT_EQUAL_INT(OP_MODE_RECOVERING, op_mode_get_current());

    publish_and_wait(EVT_OP_MODE_RECOVERY_COMPLETED, (uint32_t)RECOVERY_RESULT_IDLE);
    TEST_ASSERT_EQUAL_INT(OP_MODE_IDLE, op_mode_get_current());

}

/* EVT_ABORT_HOME_DONE → ABORT_HOMING → STOPPED */
static void test_alarm_home_done_enters_stopped(void)
{
    time_util_init();
    TEST_ASSERT_EQUAL_INT(SW_OK, event_bus_init());
    TEST_ASSERT_EQUAL_INT(SW_OK, operational_mode_init());
    TEST_ASSERT_EQUAL_INT(SW_OK, op_mode_bridge_init());
    setup_idle();

    /* 手动推进到 ABORT_HOMING */
    op_mode_on_wash_session_started();
    op_mode_on_wash_session_aborted(WASH_ABORT_CRITICAL);
    TEST_ASSERT_EQUAL_INT(OP_MODE_ABORT_HOMING, op_mode_get_current());

    publish_and_wait(EVT_ABORT_HOME_DONE, 0U);
    TEST_ASSERT_EQUAL_INT(OP_MODE_STOPPED, op_mode_get_current());

}

/* MAJOR 告警事件：已在 STOPPED 时保持 STOPPED */
static void test_blocking_alarm_event_keeps_stopped(void)
{
    static const alarm_def_t catalog[] = {
        {
         .code         = 201101U,
         .level        = ALARM_LEVEL_MAJOR,
         .clear        = ALARM_CLEAR_MANUAL_RESET,
         .reeval_group = ALARM_REEVAL_GROUP_NONE,
         .desc         = "test blocking",
         },
    };
    time_util_init();
    TEST_ASSERT_EQUAL_INT(SW_OK, event_bus_init());
    TEST_ASSERT_EQUAL_INT(SW_OK, alarm_registry_init());
    TEST_ASSERT_EQUAL_INT(SW_OK, alarm_registry_load_catalog(catalog, 1U));
    TEST_ASSERT_EQUAL_INT(SW_OK, operational_mode_init());
    TEST_ASSERT_EQUAL_INT(SW_OK, op_mode_bridge_init());
    TEST_ASSERT_EQUAL_INT(SW_OK, alarm_registry_trigger(201101U));

    publish_and_wait(EVT_ALARM_TRIGGERED, 201101U);
    TEST_ASSERT_EQUAL_INT(OP_MODE_STOPPED, op_mode_get_current());
}

int main(void)
{
    UNITY_BEGIN();

    WDF_RUN_TEST(test_hw_estop_on_enters_stopped, "", "验证HW急停开启进入停止模式");
    WDF_RUN_TEST(test_wash_session_started_enters_washing, "", "验证洗车会话已启动进入洗车模式");
    WDF_RUN_TEST(test_wash_done_enters_wash_done, "", "验证洗车完成进入洗车完成");
    WDF_RUN_TEST(test_customer_gone_returns_idle, "", "验证客户离场返回空闲模式");
    WDF_RUN_TEST(test_wash_aborted_manual_enters_alarm_homing, "", "验证洗车已中止手动进入报警回零");
    WDF_RUN_TEST(test_self_check_from_stopped_lands_stopped, "", "验证自检检查从停止模式最终进入停止模式");
    WDF_RUN_TEST(test_estop_during_self_check_enters_stopped, "", "验证自检中急停立即进入停止");
    WDF_RUN_TEST(test_recovery_completed_success_enters_idle, "", "验证恢复完成成功进入空闲模式");
    WDF_RUN_TEST(test_alarm_home_done_enters_stopped, "", "验证中止归位完成进入停止模式");
    WDF_RUN_TEST(test_blocking_alarm_event_keeps_stopped, "", "验证阻断报警在停止模式保持停止");

    return UNITY_END();
}
