/**
 * @file    test_op_mode_bridge.c
 * @brief   op_mode_bridge 运行模式事件桥接单测
 */

#include "application/bridges/op_mode_bridge.h"
#include "application/job_admit.h"
#include "common/event_types.h"
#include "common/sw_error.h"
#include "common/time_util.h"
#include "domain/job/job_types.h"
#include "domain/job/order_book.h"
#include "domain/op_mode/device_command.h"
#include "domain/op_mode/op_mode_types.h"
#include "domain/op_mode/operational_mode.h"
#include "domain/ports/outbound/device/device_ops_port.h"
#include "domain/ports/outbound/safety/safety_port.h"
#include "domain/safety/alarm_registry/alarm_registry.h"
#include "domain/safety/model/alarm_types.h"
#include "runtime/event_bus/event_bus.h"
#include "runtime/ports/port_registry.h"
#include "tests/stubs/device_ops_stub.h"
#include "tests/stubs/test_wash_modes.h"
#include "wdf_test_spec.h"
#include "tests/stubs/test_machine_profile.h"

#include <string.h>

static volatile int s_mode_changed;

static void on_mode_changed(const event_t *evt)
{
    (void)evt;
    s_mode_changed++;
}

static void publish_and_wait(event_type_t type, uint32_t param)
{
    TEST_ASSERT_EQUAL_INT(SW_OK, event_publish(type, param));
    TEST_ASSERT_EQUAL_INT(SW_OK, event_bus_drain());
}

/* 辅助：直接将 op_mode 推进到 STANDBY（绕过 event_bus）*/
static void setup_standby(void)
{
    dev_cmd_t cmd = dev_cmd_make_simple(DEV_CMD_RECOVER);

    (void)op_mode_handle_command(&cmd); /* STOPPED → RECOVERING */
    op_mode_on_recovery_completed(RECOVERY_RESULT_STANDBY);
}

static void enter_active(void)
{
    setup_standby();
    op_mode_on_job_started();
}

void setUp(void)
{
}

void tearDown(void)
{
}

static sw_err_t fake_cutout(void)
{
    return SW_OK;
}

static bool fake_estop_is_active(void)
{
    return false;
}

static bool fake_alarm_is_estop(uint32_t alarm_code)
{
    return alarm_code == 209909U;
}

static void fake_deferred_stop(void)
{
}

static const safety_ops_t s_fake_safety_ops = {
    .cutout          = fake_cutout,
    .estop_is_active = fake_estop_is_active,
    .alarm_is_estop  = fake_alarm_is_estop,
    .deferred_stop   = fake_deferred_stop,
};

static void queue_one_order(void)
{
    job_desc_t order;

    memset(&order, 0, sizeof(order));
    order.mode = TEST_WASH_MODE_A;
    TEST_ASSERT_EQUAL_INT(SW_OK, job_admit_submit(&order));
}

/* 急停触发 → STOPPED */
static void test_hw_estop_on_enters_stopped(void)
{
    time_util_init();
    TEST_ASSERT_EQUAL_INT(SW_OK, event_bus_init());
    TEST_ASSERT_EQUAL_INT(SW_OK, operational_mode_init());
    TEST_ASSERT_EQUAL_INT(SW_OK, test_machine_profile_register());
    TEST_ASSERT_EQUAL_INT(SW_OK, job_admit_init());
    TEST_ASSERT_EQUAL_INT(SW_OK, op_mode_bridge_init());
    setup_standby();
    queue_one_order();
    TEST_ASSERT_EQUAL_UINT(1U, order_book_count());
    publish_and_wait(EVT_HW_ESTOP_ON, 0U);
    TEST_ASSERT_TRUE(op_mode_is_estop_active());
    TEST_ASSERT_EQUAL_INT(OP_MODE_STOPPED, op_mode_get_current());
    TEST_ASSERT_EQUAL_UINT(0U, order_book_count());
}

/* EVT_JOB_STARTED 不再切 ACTIVE，只由放行核调用 on_job_started */
static void test_job_started_event_does_not_enter_active(void)
{
    time_util_init();
    TEST_ASSERT_EQUAL_INT(SW_OK, event_bus_init());
    TEST_ASSERT_EQUAL_INT(SW_OK, operational_mode_init());
    TEST_ASSERT_EQUAL_INT(SW_OK, test_machine_profile_register());
    TEST_ASSERT_EQUAL_INT(SW_OK, job_admit_init());
    TEST_ASSERT_EQUAL_INT(SW_OK, op_mode_bridge_init());
    setup_standby();
    publish_and_wait(EVT_JOB_STARTED, 0U);
    TEST_ASSERT_EQUAL_INT(OP_MODE_STANDBY, op_mode_get_current());
}

/* 作业完成 → HOLD */
static void test_job_completed_enters_hold(void)
{
    time_util_init();
    TEST_ASSERT_EQUAL_INT(SW_OK, event_bus_init());
    TEST_ASSERT_EQUAL_INT(SW_OK, operational_mode_init());
    TEST_ASSERT_EQUAL_INT(SW_OK, test_machine_profile_register());
    TEST_ASSERT_EQUAL_INT(SW_OK, job_admit_init());
    TEST_ASSERT_EQUAL_INT(SW_OK, op_mode_bridge_init());
    enter_active();
    publish_and_wait(EVT_JOB_COMPLETED, 1U);
    TEST_ASSERT_EQUAL_INT(OP_MODE_HOLD, op_mode_get_current());
}

/* 作业完成 + 场区清空 → STANDBY */
static void test_area_cleared_returns_standby(void)
{
    time_util_init();
    TEST_ASSERT_EQUAL_INT(SW_OK, event_bus_init());
    TEST_ASSERT_EQUAL_INT(SW_OK, operational_mode_init());
    TEST_ASSERT_EQUAL_INT(SW_OK, test_machine_profile_register());
    TEST_ASSERT_EQUAL_INT(SW_OK, job_admit_init());
    TEST_ASSERT_EQUAL_INT(SW_OK, op_mode_bridge_init());
    enter_active();
    publish_and_wait(EVT_JOB_COMPLETED, 1U);
    publish_and_wait(EVT_JOB_AREA_CLEARED, 0U);
    TEST_ASSERT_EQUAL_INT(OP_MODE_STANDBY, op_mode_get_current());
}

/* 默认 profile（单车机型）：在洗作业中止 → 整机 CLEARING */
static void test_job_aborted_requests_clear(void)
{
    static device_ops_t ops;
    job_desc_t          order;

    time_util_init();
    TEST_ASSERT_EQUAL_INT(SW_OK, event_bus_init());
    TEST_ASSERT_EQUAL_INT(SW_OK, operational_mode_init());
    TEST_ASSERT_EQUAL_INT(SW_OK, test_machine_profile_register());
    TEST_ASSERT_EQUAL_INT(SW_OK, job_admit_init());
    TEST_ASSERT_EQUAL_INT(SW_OK, op_mode_bridge_init());
    device_ops_stub_reset();
    memset(&ops, 0, sizeof(ops));
    device_ops_stub_bind(&ops);
    device_ops_register(&ops);
    setup_standby();
    device_ops_stub_set_admit_ready(true);
    memset(&order, 0, sizeof(order));
    order.mode = TEST_WASH_MODE_A;
    TEST_ASSERT_EQUAL_INT(SW_OK, job_admit_submit(&order));
    TEST_ASSERT_EQUAL_INT(SW_OK, job_admit_start());
    TEST_ASSERT_EQUAL_INT(OP_MODE_ACTIVE, op_mode_get_current());

    publish_and_wait(EVT_JOB_ABORTED, (uint32_t)device_ops_stub_last_job_id());
    TEST_ASSERT_EQUAL_INT(OP_MODE_CLEARING, op_mode_get_current());
    device_ops_register(NULL);
}

/* 自检完成（从 STOPPED 出发，成功）→ STOPPED */
static void test_self_check_from_stopped_lands_stopped(void)
{
    dev_cmd_t cmd = dev_cmd_make_simple(DEV_CMD_START_SELF_CHECK);
    time_util_init();
    TEST_ASSERT_EQUAL_INT(SW_OK, event_bus_init());
    TEST_ASSERT_EQUAL_INT(SW_OK, operational_mode_init());
    TEST_ASSERT_EQUAL_INT(SW_OK, op_mode_bridge_init());
    (void)op_mode_handle_command(&cmd);                     /* STOPPED → SELF_CHECK */
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

    publish_and_wait(EVT_OP_MODE_RECOVERY_COMPLETED, (uint32_t)RECOVERY_RESULT_STANDBY);
    TEST_ASSERT_EQUAL_INT(OP_MODE_STANDBY, op_mode_get_current());
}

/* EVT_CLEAR_MACHINE_DONE → ABORT_HOMING → STOPPED */
static void test_alarm_home_done_enters_stopped(void)
{
    time_util_init();
    TEST_ASSERT_EQUAL_INT(SW_OK, event_bus_init());
    TEST_ASSERT_EQUAL_INT(SW_OK, operational_mode_init());
    TEST_ASSERT_EQUAL_INT(SW_OK, op_mode_bridge_init());
    setup_standby();

    /* 手动推进到 CLEARING */
    op_mode_on_job_started();
    op_mode_on_clear_requested(WASH_ABORT_CRITICAL);
    TEST_ASSERT_EQUAL_INT(OP_MODE_CLEARING, op_mode_get_current());

    publish_and_wait(EVT_CLEAR_MACHINE_DONE, 0U);
    TEST_ASSERT_EQUAL_INT(OP_MODE_STOPPED, op_mode_get_current());
}

/* MAJOR 告警事件：已在 STOPPED 时保持 STOPPED */
static void test_blocking_alarm_event_keeps_stopped(void)
{
    static const alarm_def_t catalog[] = {
        {
         .code         = 201101U,
         .level        = ALARM_LEVEL_MAJOR,
         .lifecycle        = ALARM_LIFECYCLE_LATCH,
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
    TEST_ASSERT_EQUAL_INT(SW_OK, event_bus_drain());
    TEST_ASSERT_EQUAL_INT(OP_MODE_STOPPED, op_mode_get_current());
}

static void test_blocking_alarm_from_standby_clears_book(void)
{
    static const alarm_def_t catalog[] = {
        {
         .code         = 201101U,
         .level        = ALARM_LEVEL_MAJOR,
         .lifecycle        = ALARM_LIFECYCLE_LATCH,
         .reeval_group = ALARM_REEVAL_GROUP_NONE,
         .desc         = "test blocking",
         },
    };

    time_util_init();
    TEST_ASSERT_EQUAL_INT(SW_OK, event_bus_init());
    TEST_ASSERT_EQUAL_INT(SW_OK, alarm_registry_init());
    TEST_ASSERT_EQUAL_INT(SW_OK, alarm_registry_load_catalog(catalog, 1U));
    TEST_ASSERT_EQUAL_INT(SW_OK, operational_mode_init());
    TEST_ASSERT_EQUAL_INT(SW_OK, test_machine_profile_register());
    TEST_ASSERT_EQUAL_INT(SW_OK, job_admit_init());
    TEST_ASSERT_EQUAL_INT(SW_OK, op_mode_bridge_init());
    setup_standby();
    queue_one_order();
    TEST_ASSERT_EQUAL_INT(SW_OK, alarm_registry_trigger(201101U));
    TEST_ASSERT_EQUAL_INT(SW_OK, event_bus_drain());
    TEST_ASSERT_EQUAL_INT(OP_MODE_STOPPED, op_mode_get_current());
    TEST_ASSERT_EQUAL_UINT(0U, order_book_count());
}

static void test_recovery_failed_clears_book(void)
{
    job_desc_t order;
    dev_cmd_t  recover = dev_cmd_make_simple(DEV_CMD_RECOVER);

    time_util_init();
    TEST_ASSERT_EQUAL_INT(SW_OK, event_bus_init());
    TEST_ASSERT_EQUAL_INT(SW_OK, operational_mode_init());
    TEST_ASSERT_EQUAL_INT(SW_OK, test_machine_profile_register());
    TEST_ASSERT_EQUAL_INT(SW_OK, job_admit_init());
    TEST_ASSERT_EQUAL_INT(SW_OK, op_mode_bridge_init());
    (void)op_mode_handle_command(&recover);
    TEST_ASSERT_EQUAL_INT(OP_MODE_RECOVERING, op_mode_get_current());
    memset(&order, 0, sizeof(order));
    order.mode = TEST_WASH_MODE_A;
    TEST_ASSERT_EQUAL_INT(SW_OK, job_admit_submit(&order));
    TEST_ASSERT_EQUAL_UINT(1U, order_book_count());
    publish_and_wait(EVT_OP_MODE_RECOVERY_COMPLETED, (uint32_t)RECOVERY_RESULT_FAILED);
    TEST_ASSERT_EQUAL_INT(OP_MODE_STOPPED, op_mode_get_current());
    TEST_ASSERT_EQUAL_UINT(0U, order_book_count());
}

static void test_clear_done_clears_queued(void)
{
    time_util_init();
    TEST_ASSERT_EQUAL_INT(SW_OK, event_bus_init());
    TEST_ASSERT_EQUAL_INT(SW_OK, operational_mode_init());
    TEST_ASSERT_EQUAL_INT(SW_OK, test_machine_profile_register());
    TEST_ASSERT_EQUAL_INT(SW_OK, job_admit_init());
    TEST_ASSERT_EQUAL_INT(SW_OK, op_mode_bridge_init());
    setup_standby();
    queue_one_order();
    op_mode_on_job_started();
    op_mode_on_clear_requested(WASH_ABORT_MANUAL);
    TEST_ASSERT_EQUAL_INT(OP_MODE_CLEARING, op_mode_get_current());
    TEST_ASSERT_EQUAL_UINT(1U, order_book_count());
    publish_and_wait(EVT_CLEAR_MACHINE_DONE, 0U);
    TEST_ASSERT_EQUAL_INT(OP_MODE_STOPPED, op_mode_get_current());
    TEST_ASSERT_EQUAL_UINT(0U, order_book_count());
}

/* 急停报警不再置 estop_active，只认 EVT_HW_ESTOP_* */
static void test_estop_alarm_does_not_set_estop_flag(void)
{
    time_util_init();
    port_registry_safety_reset();
    TEST_ASSERT_EQUAL_INT(SW_OK, event_bus_init());
    TEST_ASSERT_EQUAL_INT(SW_OK, operational_mode_init());
    TEST_ASSERT_EQUAL_INT(SW_OK, safety_port_register(&s_fake_safety_ops));
    TEST_ASSERT_EQUAL_INT(SW_OK, op_mode_bridge_init());

    publish_and_wait(EVT_ALARM_TRIGGERED, 209909U);
    TEST_ASSERT_FALSE(op_mode_is_estop_active());
    TEST_ASSERT_EQUAL_INT(OP_MODE_STOPPED, op_mode_get_current());

    publish_and_wait(EVT_HW_ESTOP_ON, 0U);
    TEST_ASSERT_TRUE(op_mode_is_estop_active());
    TEST_ASSERT_EQUAL_INT(OP_MODE_STOPPED, op_mode_get_current());
    port_registry_safety_reset();
}

/* CRITICAL 从 IDLE 进入 STOPPED 只经 blocking 收敛一次 */
static void test_critical_from_idle_converges_once(void)
{
    static const alarm_def_t catalog[] = {
        {
         .code         = 209909U,
         .level        = ALARM_LEVEL_CRITICAL,
         .lifecycle        = ALARM_LIFECYCLE_FOLLOW_SIGNAL,
         .reeval_group = ALARM_REEVAL_GROUP_NONE,
         .desc         = "test critical",
         },
    };

    time_util_init();
    TEST_ASSERT_EQUAL_INT(SW_OK, event_bus_init());
    TEST_ASSERT_EQUAL_INT(SW_OK, alarm_registry_init());
    TEST_ASSERT_EQUAL_INT(SW_OK, alarm_registry_load_catalog(catalog, 1U));
    TEST_ASSERT_EQUAL_INT(SW_OK, operational_mode_init());
    TEST_ASSERT_EQUAL_INT(SW_OK, op_mode_bridge_init());
    setup_standby();
    TEST_ASSERT_EQUAL_INT(SW_OK, event_bus_drain());
    TEST_ASSERT_EQUAL_INT(OP_MODE_STANDBY, op_mode_get_current());

    s_mode_changed = 0;
    TEST_ASSERT_EQUAL_INT(SW_OK, event_subscribe(EVT_OP_MODE_CHANGED, on_mode_changed));
    TEST_ASSERT_EQUAL_INT(SW_OK, alarm_registry_trigger(209909U));
    TEST_ASSERT_EQUAL_INT(SW_OK, event_bus_drain());

    TEST_ASSERT_EQUAL_INT(OP_MODE_STOPPED, op_mode_get_current());
    TEST_ASSERT_EQUAL_INT(1, s_mode_changed);
}

int main(void)
{
    UNITY_BEGIN();

    WDF_RUN_TEST(test_hw_estop_on_enters_stopped, "", "验证HW急停进入停止并清空订单簿");
    WDF_RUN_TEST(test_job_started_event_does_not_enter_active, "", "验证作业开始事件不切 ACTIVE");
    WDF_RUN_TEST(test_job_completed_enters_hold, "", "验证作业完成进入 HOLD");
    WDF_RUN_TEST(test_area_cleared_returns_standby, "", "验证场区清空返回 STANDBY");
    WDF_RUN_TEST(test_job_aborted_requests_clear, "", "验证单车机型在洗作业中止后进入清障");
    WDF_RUN_TEST(test_self_check_from_stopped_lands_stopped, "", "验证自检检查从停止模式最终进入停止模式");
    WDF_RUN_TEST(test_estop_during_self_check_enters_stopped, "", "验证自检中急停立即进入停止");
    WDF_RUN_TEST(test_recovery_completed_success_enters_idle, "", "验证恢复完成成功进入空闲模式");
    WDF_RUN_TEST(test_alarm_home_done_enters_stopped, "", "验证中止归位完成进入停止模式");
    WDF_RUN_TEST(test_blocking_alarm_event_keeps_stopped, "", "验证阻断报警在停止模式保持停止");
    WDF_RUN_TEST(test_blocking_alarm_from_standby_clears_book, "", "验证待机禁止接单停机清空订单簿");
    WDF_RUN_TEST(test_recovery_failed_clears_book, "", "验证恢复失败落到停机清空订单簿");
    WDF_RUN_TEST(test_clear_done_clears_queued, "", "验证中止归位完成进入停机时清空订单簿");
    WDF_RUN_TEST(test_critical_from_idle_converges_once, "", "验证 CRITICAL 从空闲只收敛一次进入停止");
    WDF_RUN_TEST(test_estop_alarm_does_not_set_estop_flag, "", "验证急停报警不再置急停旗标");

    return UNITY_END();
}
