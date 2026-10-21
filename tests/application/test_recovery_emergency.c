/**
 * @file    test_recovery_emergency.c
 * @brief   recovery_coordinator / safety_session_coordinator 单元测试
 */

#include "adapters/outbound/safety/sim/hw_estop_sim.h"
#include "application/bridges/op_mode_bridge.h"
#include "application/orchestrators/recovery_coordinator.h"
#include "application/orchestrators/safety_session_coordinator.h"
#include "application/side_effect_router.h"
#include "common/event_types.h"
#include "common/sw_error.h"
#include "common/time_util.h"
#include "domain/op_mode/device_command.h"
#include "domain/op_mode/op_mode_types.h"
#include "domain/op_mode/operational_mode.h"
#include "domain/ports/outbound/device/device_ops_port.h"
#include "domain/ports/outbound/safety/safety_port.h"
#include "domain/safety/alarm_registry/alarm_registry.h"
#include "domain/safety/model/alarm_types.h"
#include "runtime/event_bus/event_bus.h"
#include "wdf_test_spec.h"

#include <string.h>

static volatile int                s_recovery_completed_count;
static volatile uint32_t           s_recovery_result_param;
static volatile int                s_abort_home_count;
static volatile int                s_home_device_count;
static volatile int                s_abort_count;
static volatile wash_abort_cause_t s_abort_cause;
static volatile int                s_deferred_stop_count;
static volatile int                s_stop_all_outputs_count;
static uint32_t                    s_retrigger_on_home_code;
static uint32_t                    s_clear_on_home_code;
static volatile int                 s_home_auto_complete;
static volatile int                 s_clear_alarms_count;
static device_clear_alarms_status_t s_clear_poll_status;

#define TEST_BLOCKING_ALARM_CODE    201101U
#define TEST_LOCKOUT_ALARM_CODE     201102U
#define TEST_LATCH_ALARM_CODE       201103U
#define TEST_SAFETY_ALARM_CODE      201109U
#define TEST_LATCH_REEVAL_GROUP     ((motion_reeval_group_id_t)1U)

static sw_err_t fake_cutout(void)
{
    return SW_OK;
}

static bool fake_estop_is_active(void)
{
    return false;
}

static bool fake_alarm_is_estop(uint32_t code)
{
    (void)code;
    return false;
}

static void fake_deferred_stop(void)
{
    s_deferred_stop_count++;
}

static const safety_ops_t s_safety_ops = {
    .cutout          = fake_cutout,
    .estop_is_active = fake_estop_is_active,
    .alarm_is_estop  = fake_alarm_is_estop,
    .deferred_stop   = fake_deferred_stop,
};

static void stub_abort_wash(wash_abort_cause_t cause)
{
    s_abort_count++;
    s_abort_cause = cause;
}

static void stub_abort_home(void)
{
    s_abort_home_count++;
    (void)event_publish(EVT_CLEAR_MACHINE_DONE, (uint32_t)SW_OK);
}

static sw_err_t stub_home_device(void)
{
    s_home_device_count++;
    if (s_retrigger_on_home_code != 0U) {
        (void)alarm_registry_trigger(s_retrigger_on_home_code);
    }
    if (s_clear_on_home_code != 0U) {
        (void)alarm_registry_clear(s_clear_on_home_code);
    }
    if (s_home_auto_complete != 0) {
        (void)event_publish(EVT_OP_MODE_HOME_COMPLETED, 1U);
    }
    return SW_OK;
}

static sw_err_t stub_stop_all_outputs(void)
{
    s_stop_all_outputs_count++;
    return SW_OK;
}

static sw_err_t stub_clear_alarms(void)
{
    s_clear_alarms_count++;
    return SW_OK;
}

static device_clear_alarms_status_t stub_poll_clear_alarms(void)
{
    return s_clear_poll_status;
}

static device_ops_t s_device_ops;

static const alarm_def_t s_blocking_catalog[] = {
    {
     .code         = TEST_BLOCKING_ALARM_CODE,
     .level        = ALARM_LEVEL_MAJOR,
     .lifecycle    = ALARM_LIFECYCLE_LATCH,
     .reeval_group = ALARM_REEVAL_GROUP_NONE,
     .desc         = "test blocking",
     },
};

static const alarm_def_t s_latch_catalog[] = {
    {
     .code         = TEST_LATCH_ALARM_CODE,
     .level        = ALARM_LEVEL_MAJOR,
     .lifecycle    = ALARM_LIFECYCLE_LATCH,
     .reeval_group = TEST_LATCH_REEVAL_GROUP,
     .desc         = "test latch",
     },
};

static const alarm_def_t s_lockout_catalog[] = {
    {
     .code         = TEST_LOCKOUT_ALARM_CODE,
     .level        = ALARM_LEVEL_CRITICAL,
     .lifecycle    = ALARM_LIFECYCLE_LATCH,
     .reeval_group = ALARM_REEVAL_GROUP_NONE,
     .desc         = "test lockout",
     },
};

static const alarm_def_t s_safety_catalog[] = {
    {
     .code         = TEST_SAFETY_ALARM_CODE,
     .level        = ALARM_LEVEL_CRITICAL,
     .lifecycle    = ALARM_LIFECYCLE_FOLLOW_SIGNAL,
     .reeval_group = ALARM_REEVAL_GROUP_NONE,
     .desc         = "test safety",
     },
};

static void on_recovery_completed(const event_t *evt)
{
    s_recovery_completed_count++;
    s_recovery_result_param = evt->param;
}

void setUp(void)
{
    s_recovery_completed_count = 0;
    s_recovery_result_param    = 0U;
    s_abort_home_count         = 0;
    s_home_device_count        = 0;
    s_abort_count              = 0;
    s_abort_cause              = WASH_ABORT_MANUAL;
    s_deferred_stop_count      = 0;
    s_stop_all_outputs_count   = 0;
    s_retrigger_on_home_code   = 0U;
    s_clear_on_home_code       = 0U;
    s_home_auto_complete       = 1;
    s_clear_alarms_count       = 0;
    s_clear_poll_status        = DEVICE_CLEAR_ALARMS_DONE;

    memset(&s_device_ops, 0, sizeof(s_device_ops));
    s_device_ops.clear_machine    = stub_abort_home;
    s_device_ops.home_device      = stub_home_device;
    s_device_ops.abort_jobs       = stub_abort_wash;
    s_device_ops.stop_all_outputs = stub_stop_all_outputs;

    time_util_init();
    TEST_ASSERT_EQUAL_INT(SW_OK, safety_port_register(&s_safety_ops));
}

void tearDown(void)
{
}

/**
 * @brief  经 RECOVER 命令进入 RECOVERING 并触发 recovery_coordinator（模式守卫要求当前为 RECOVERING）
 */
static void start_recover_via_command(void)
{
    dev_cmd_t cmd = dev_cmd_make_simple(DEV_CMD_RECOVER);

    TEST_ASSERT_EQUAL_INT(SW_OK, operational_mode_init());
    TEST_ASSERT_EQUAL_INT(OP_CMD_ALLOWED, op_mode_handle_command(&cmd).verdict);
    TEST_ASSERT_EQUAL_INT(OP_MODE_RECOVERING, op_mode_get_current());
}

static void test_recovery_coordinator_publishes_completed_idle(void)
{
    TEST_ASSERT_EQUAL_INT(SW_OK, event_bus_init());
    TEST_ASSERT_EQUAL_INT(SW_OK, alarm_registry_init());
    device_ops_register(&s_device_ops);
    TEST_ASSERT_EQUAL_INT(SW_OK, recovery_coordinator_init());
    TEST_ASSERT_EQUAL_INT(SW_OK, event_subscribe(EVT_OP_MODE_RECOVERY_COMPLETED, on_recovery_completed));

    start_recover_via_command();
    TEST_ASSERT_EQUAL_INT(SW_OK, event_bus_drain());

    TEST_ASSERT_EQUAL_INT(1, s_recovery_completed_count);
    TEST_ASSERT_EQUAL_UINT32((uint32_t)RECOVERY_RESULT_STANDBY, s_recovery_result_param);
    TEST_ASSERT_EQUAL_INT(1, s_home_device_count);
}

static void test_recovery_coordinator_keeps_exception_when_blocking_remains(void)
{
    TEST_ASSERT_EQUAL_INT(SW_OK, event_bus_init());
    TEST_ASSERT_EQUAL_INT(SW_OK, alarm_registry_init());
    TEST_ASSERT_EQUAL_INT(SW_OK, alarm_registry_load_catalog(s_blocking_catalog, 1U));
    TEST_ASSERT_EQUAL_INT(SW_OK, alarm_registry_trigger(TEST_BLOCKING_ALARM_CODE));
    s_retrigger_on_home_code = TEST_BLOCKING_ALARM_CODE;
    device_ops_register(&s_device_ops);
    TEST_ASSERT_EQUAL_INT(SW_OK, recovery_coordinator_init());
    TEST_ASSERT_EQUAL_INT(SW_OK, event_subscribe(EVT_OP_MODE_RECOVERY_COMPLETED, on_recovery_completed));

    start_recover_via_command();
    TEST_ASSERT_EQUAL_INT(SW_OK, event_bus_drain());

    TEST_ASSERT_EQUAL_INT(1, s_recovery_completed_count);
    TEST_ASSERT_EQUAL_UINT32((uint32_t)RECOVERY_RESULT_FAILED, s_recovery_result_param);
    TEST_ASSERT_EQUAL_INT(1, s_home_device_count);
}

/**
 * 归位中条件消失的事件锁存在归位成功后摘掉，进待机。
 */
static void test_recovery_resets_latch_cleared_during_home(void)
{
    TEST_ASSERT_EQUAL_INT(SW_OK, event_bus_init());
    TEST_ASSERT_EQUAL_INT(SW_OK, alarm_registry_init());
    TEST_ASSERT_EQUAL_INT(SW_OK, alarm_registry_load_catalog(s_blocking_catalog, 1U));
    TEST_ASSERT_EQUAL_INT(SW_OK, alarm_registry_trigger(TEST_BLOCKING_ALARM_CODE));
    s_retrigger_on_home_code = TEST_BLOCKING_ALARM_CODE;
    s_clear_on_home_code     = TEST_BLOCKING_ALARM_CODE;
    device_ops_register(&s_device_ops);
    TEST_ASSERT_EQUAL_INT(SW_OK, recovery_coordinator_init());
    TEST_ASSERT_EQUAL_INT(SW_OK, event_subscribe(EVT_OP_MODE_RECOVERY_COMPLETED, on_recovery_completed));

    start_recover_via_command();
    TEST_ASSERT_EQUAL_INT(SW_OK, event_bus_drain());

    TEST_ASSERT_EQUAL_INT(1, s_recovery_completed_count);
    TEST_ASSERT_EQUAL_UINT32((uint32_t)RECOVERY_RESULT_STANDBY, s_recovery_result_param);
    TEST_ASSERT_FALSE(alarm_registry_is_active(TEST_BLOCKING_ALARM_CODE));
    TEST_ASSERT_EQUAL_INT(1, s_home_device_count);
}

static void test_recovery_safety_trigger_blocks_home(void)
{
    TEST_ASSERT_EQUAL_INT(SW_OK, event_bus_init());
    TEST_ASSERT_EQUAL_INT(SW_OK, alarm_registry_init());
    TEST_ASSERT_EQUAL_INT(SW_OK, alarm_registry_load_catalog(s_safety_catalog, 1U));
    TEST_ASSERT_EQUAL_INT(SW_OK, alarm_registry_trigger(TEST_SAFETY_ALARM_CODE));
    device_ops_register(&s_device_ops);
    TEST_ASSERT_EQUAL_INT(SW_OK, recovery_coordinator_init());
    TEST_ASSERT_EQUAL_INT(SW_OK, event_subscribe(EVT_OP_MODE_RECOVERY_COMPLETED, on_recovery_completed));

    start_recover_via_command();
    TEST_ASSERT_EQUAL_INT(SW_OK, event_bus_drain());

    TEST_ASSERT_EQUAL_INT(1, s_recovery_completed_count);
    TEST_ASSERT_EQUAL_UINT32((uint32_t)RECOVERY_RESULT_FAILED, s_recovery_result_param);
    TEST_ASSERT_EQUAL_INT(0, s_home_device_count);
    TEST_ASSERT_TRUE(alarm_registry_is_active(TEST_SAFETY_ALARM_CODE));
}

/**
 * 非安全 MAJOR 条件仍成立也强制清掉，归位可以开跑。
 */
static void test_recovery_clears_non_safety_before_home(void)
{
    TEST_ASSERT_EQUAL_INT(SW_OK, event_bus_init());
    TEST_ASSERT_EQUAL_INT(SW_OK, alarm_registry_init());
    TEST_ASSERT_EQUAL_INT(SW_OK, alarm_registry_load_catalog(s_blocking_catalog, 1U));
    TEST_ASSERT_EQUAL_INT(SW_OK, alarm_registry_trigger(TEST_BLOCKING_ALARM_CODE));
    device_ops_register(&s_device_ops);
    TEST_ASSERT_EQUAL_INT(SW_OK, recovery_coordinator_init());
    TEST_ASSERT_EQUAL_INT(SW_OK, event_subscribe(EVT_OP_MODE_RECOVERY_COMPLETED, on_recovery_completed));

    start_recover_via_command();
    TEST_ASSERT_EQUAL_INT(SW_OK, event_bus_drain());

    TEST_ASSERT_EQUAL_INT(1, s_recovery_completed_count);
    TEST_ASSERT_EQUAL_UINT32((uint32_t)RECOVERY_RESULT_STANDBY, s_recovery_result_param);
    TEST_ASSERT_FALSE(alarm_registry_is_active(TEST_BLOCKING_ALARM_CODE));
    TEST_ASSERT_EQUAL_INT(1, s_home_device_count);
}

/**
 * 非安全 CRITICAL 不挡起步；起步强制清掉后归位。
 */
static void test_recovery_critical_non_safety_starts_home(void)
{
    TEST_ASSERT_EQUAL_INT(SW_OK, event_bus_init());
    TEST_ASSERT_EQUAL_INT(SW_OK, alarm_registry_init());
    TEST_ASSERT_EQUAL_INT(SW_OK, alarm_registry_load_catalog(s_lockout_catalog, 1U));
    TEST_ASSERT_EQUAL_INT(SW_OK, alarm_registry_trigger(TEST_LOCKOUT_ALARM_CODE));
    TEST_ASSERT_EQUAL_INT(SAFETY_POSTURE_LOCKOUT, alarm_registry_safety_posture());
    device_ops_register(&s_device_ops);
    TEST_ASSERT_EQUAL_INT(SW_OK, recovery_coordinator_init());
    TEST_ASSERT_EQUAL_INT(SW_OK, event_subscribe(EVT_OP_MODE_RECOVERY_COMPLETED, on_recovery_completed));

    start_recover_via_command();
    TEST_ASSERT_EQUAL_INT(SW_OK, event_bus_drain());

    TEST_ASSERT_EQUAL_INT(1, s_recovery_completed_count);
    TEST_ASSERT_EQUAL_UINT32((uint32_t)RECOVERY_RESULT_STANDBY, s_recovery_result_param);
    TEST_ASSERT_FALSE(alarm_registry_is_active(TEST_LOCKOUT_ALARM_CODE));
    TEST_ASSERT_EQUAL_INT(1, s_home_device_count);
}

static void test_recovery_resets_inactive_lockout_before_home(void)
{
    TEST_ASSERT_EQUAL_INT(SW_OK, event_bus_init());
    TEST_ASSERT_EQUAL_INT(SW_OK, alarm_registry_init());
    TEST_ASSERT_EQUAL_INT(SW_OK, alarm_registry_load_catalog(s_lockout_catalog, 1U));
    TEST_ASSERT_EQUAL_INT(SW_OK, alarm_registry_trigger(TEST_LOCKOUT_ALARM_CODE));
    TEST_ASSERT_EQUAL_INT(SW_OK, alarm_registry_clear(TEST_LOCKOUT_ALARM_CODE));
    TEST_ASSERT_EQUAL_INT(SAFETY_POSTURE_LOCKOUT, alarm_registry_safety_posture());
    device_ops_register(&s_device_ops);
    TEST_ASSERT_EQUAL_INT(SW_OK, recovery_coordinator_init());
    TEST_ASSERT_EQUAL_INT(SW_OK, event_subscribe(EVT_OP_MODE_RECOVERY_COMPLETED, on_recovery_completed));

    start_recover_via_command();
    TEST_ASSERT_EQUAL_INT(SW_OK, event_bus_drain());

    TEST_ASSERT_EQUAL_INT(1, s_recovery_completed_count);
    TEST_ASSERT_EQUAL_UINT32((uint32_t)RECOVERY_RESULT_STANDBY, s_recovery_result_param);
    TEST_ASSERT_FALSE(alarm_registry_is_active(TEST_LOCKOUT_ALARM_CODE));
    TEST_ASSERT_EQUAL_INT(1, s_home_device_count);
}

static void test_cutout_estop_release_does_not_run_abort_home(void)
{
    TEST_ASSERT_EQUAL_INT(SW_OK, event_bus_init());
    device_ops_register(&s_device_ops);
    TEST_ASSERT_EQUAL_INT(SW_OK, safety_session_coordinator_init());

    TEST_ASSERT_EQUAL_INT(SW_OK, event_publish(EVT_HW_ESTOP_OFF, 0U));
    TEST_ASSERT_EQUAL_INT(SW_OK, event_bus_drain());

    TEST_ASSERT_EQUAL_INT(0, s_abort_home_count);
}

static void test_cutout_lockout_aborts_wash(void)
{
    TEST_ASSERT_EQUAL_INT(SW_OK, event_bus_init());
    device_ops_register(&s_device_ops);
    TEST_ASSERT_EQUAL_INT(SW_OK, safety_session_coordinator_init());

    TEST_ASSERT_EQUAL_INT(SW_OK, event_publish(EVT_SAFETY_LOCKOUT, 0U));
    TEST_ASSERT_EQUAL_INT(SW_OK, event_bus_drain());

    TEST_ASSERT_EQUAL_INT(1, s_abort_count);
    TEST_ASSERT_EQUAL_INT(WASH_ABORT_CRITICAL, s_abort_cause);
    TEST_ASSERT_EQUAL_INT(0, s_deferred_stop_count);
}

static void test_abort_home_requested_runs_abort_home(void)
{
    TEST_ASSERT_EQUAL_INT(SW_OK, event_bus_init());
    device_ops_register(&s_device_ops);
    TEST_ASSERT_EQUAL_INT(SW_OK, safety_session_coordinator_init());

    TEST_ASSERT_EQUAL_INT(SW_OK, event_publish(EVT_CLEAR_MACHINE_REQUESTED, 0U));
    TEST_ASSERT_EQUAL_INT(SW_OK, event_bus_drain());

    TEST_ASSERT_EQUAL_INT(1, s_abort_home_count);
}

static void test_cutout_estop_on_aborts_wash_and_defers_stop(void)
{
    TEST_ASSERT_EQUAL_INT(SW_OK, event_bus_init());
    device_ops_register(&s_device_ops);
    TEST_ASSERT_EQUAL_INT(SW_OK, safety_session_coordinator_init());

    TEST_ASSERT_EQUAL_INT(SW_OK, event_publish(EVT_HW_ESTOP_ON, 0U));
    TEST_ASSERT_EQUAL_INT(SW_OK, event_bus_drain());

    TEST_ASSERT_EQUAL_INT(1, s_deferred_stop_count);
    TEST_ASSERT_EQUAL_INT(1, s_abort_count);
    TEST_ASSERT_EQUAL_INT(WASH_ABORT_ESTOP, s_abort_cause);
    TEST_ASSERT_EQUAL_INT(0, s_abort_home_count);
}

/**
 * RECOVERING → STOP_ALL → 迟到 HOME_COMPLETED：保持 STOPPED，不发 RECOVERY_COMPLETED
 */
static void test_leave_recovering_ignores_late_home_completed(void)
{
    dev_cmd_t recover  = dev_cmd_make_simple(DEV_CMD_RECOVER);
    dev_cmd_t stop_all = dev_cmd_make_simple(DEV_CMD_STOP_ALL_OUTPUTS);

    s_home_auto_complete = 0;

    TEST_ASSERT_EQUAL_INT(SW_OK, event_bus_init());
    TEST_ASSERT_EQUAL_INT(SW_OK, alarm_registry_init());
    TEST_ASSERT_EQUAL_INT(SW_OK, operational_mode_init());
    device_ops_register(&s_device_ops);
    TEST_ASSERT_EQUAL_INT(SW_OK, recovery_coordinator_init());
    TEST_ASSERT_EQUAL_INT(SW_OK, event_subscribe(EVT_OP_MODE_RECOVERY_COMPLETED, on_recovery_completed));

    TEST_ASSERT_EQUAL_INT(OP_CMD_ALLOWED, op_mode_handle_command(&recover).verdict);
    TEST_ASSERT_EQUAL_INT(OP_MODE_RECOVERING, op_mode_get_current());
    /* drain RECOVERY_REQUESTED → home 启动但不自动完成 */
    TEST_ASSERT_EQUAL_INT(SW_OK, event_bus_drain());
    TEST_ASSERT_EQUAL_INT(1, s_home_device_count);
    TEST_ASSERT_EQUAL_INT(0, s_recovery_completed_count);

    TEST_ASSERT_EQUAL_INT(OP_CMD_ALLOWED, op_mode_handle_command(&stop_all).verdict);
    TEST_ASSERT_EQUAL_INT(OP_MODE_STOPPED, op_mode_get_current());
    TEST_ASSERT_EQUAL_INT(SW_OK, event_bus_drain()); /* MODE_CHANGED → cancel_pending */

    TEST_ASSERT_EQUAL_INT(SW_OK, event_publish(EVT_OP_MODE_HOME_COMPLETED, 1U));
    TEST_ASSERT_EQUAL_INT(SW_OK, event_bus_drain());

    TEST_ASSERT_EQUAL_INT(0, s_recovery_completed_count);
    TEST_ASSERT_EQUAL_INT(OP_MODE_STOPPED, op_mode_get_current());
}

/**
 * 归位等待中急停：立即 deferred_stop + abort，模式进 STOPPED；迟到 HOME 不复活恢复。
 */
static void test_estop_during_pending_home_preempts_recovery(void)
{
    dev_cmd_t recover = dev_cmd_make_simple(DEV_CMD_RECOVER);

    s_home_auto_complete = 0;

    TEST_ASSERT_EQUAL_INT(SW_OK, event_bus_init());
    TEST_ASSERT_EQUAL_INT(SW_OK, alarm_registry_init());
    TEST_ASSERT_EQUAL_INT(SW_OK, operational_mode_init());
    device_ops_register(&s_device_ops);
    TEST_ASSERT_EQUAL_INT(SW_OK, recovery_coordinator_init());
    TEST_ASSERT_EQUAL_INT(SW_OK, safety_session_coordinator_init());
    TEST_ASSERT_EQUAL_INT(SW_OK, op_mode_bridge_init());
    TEST_ASSERT_EQUAL_INT(SW_OK, event_subscribe(EVT_OP_MODE_RECOVERY_COMPLETED, on_recovery_completed));

    TEST_ASSERT_EQUAL_INT(OP_CMD_ALLOWED, op_mode_handle_command(&recover).verdict);
    TEST_ASSERT_EQUAL_INT(SW_OK, event_bus_drain());
    TEST_ASSERT_EQUAL_INT(OP_MODE_RECOVERING, op_mode_get_current());
    TEST_ASSERT_EQUAL_INT(1, s_home_device_count);

    TEST_ASSERT_EQUAL_INT(SW_OK, event_publish(EVT_HW_ESTOP_ON, 0U));
    TEST_ASSERT_EQUAL_INT(SW_OK, event_bus_drain());

    TEST_ASSERT_EQUAL_INT(1, s_deferred_stop_count);
    TEST_ASSERT_EQUAL_INT(1, s_abort_count);
    TEST_ASSERT_EQUAL_INT(WASH_ABORT_ESTOP, s_abort_cause);
    TEST_ASSERT_TRUE(op_mode_is_estop_active());
    TEST_ASSERT_EQUAL_INT(OP_MODE_STOPPED, op_mode_get_current());

    TEST_ASSERT_EQUAL_INT(SW_OK, event_publish(EVT_OP_MODE_HOME_COMPLETED, 1U));
    TEST_ASSERT_EQUAL_INT(SW_OK, event_bus_drain());
    TEST_ASSERT_EQUAL_INT(0, s_recovery_completed_count);
    TEST_ASSERT_EQUAL_INT(OP_MODE_STOPPED, op_mode_get_current());
}

/**
 * 归位等待中 STOP_ALL：切 STOPPED 并切断输出，不等 HOME 完成；迟到 HOME 忽略。
 */
static void test_stop_all_during_pending_home_cuts_outputs(void)
{
    operational_mode_t mode_before;
    dev_cmd_t          recover  = dev_cmd_make_simple(DEV_CMD_RECOVER);
    dev_cmd_t          stop_all = dev_cmd_make_simple(DEV_CMD_STOP_ALL_OUTPUTS);

    s_home_auto_complete = 0;

    TEST_ASSERT_EQUAL_INT(SW_OK, event_bus_init());
    TEST_ASSERT_EQUAL_INT(SW_OK, alarm_registry_init());
    TEST_ASSERT_EQUAL_INT(SW_OK, operational_mode_init());
    device_ops_register(&s_device_ops);
    TEST_ASSERT_EQUAL_INT(SW_OK, recovery_coordinator_init());
    TEST_ASSERT_EQUAL_INT(SW_OK, event_subscribe(EVT_OP_MODE_RECOVERY_COMPLETED, on_recovery_completed));

    TEST_ASSERT_EQUAL_INT(OP_CMD_ALLOWED, op_mode_handle_command(&recover).verdict);
    TEST_ASSERT_EQUAL_INT(SW_OK, event_bus_drain());
    TEST_ASSERT_EQUAL_INT(OP_MODE_RECOVERING, op_mode_get_current());
    TEST_ASSERT_EQUAL_INT(1, s_home_device_count);

    mode_before = op_mode_get_current();
    TEST_ASSERT_EQUAL_INT(OP_CMD_ALLOWED, op_mode_handle_command(&stop_all).verdict);
    TEST_ASSERT_EQUAL_INT(OP_MODE_STOPPED, op_mode_get_current());
    TEST_ASSERT_EQUAL_INT(SW_OK, side_effect_router_run(&stop_all, mode_before));
    TEST_ASSERT_EQUAL_INT(1, s_stop_all_outputs_count);
    TEST_ASSERT_EQUAL_INT(0, s_abort_count); /* 前态非 WASHING，不 abort 会话 */

    TEST_ASSERT_EQUAL_INT(SW_OK, event_bus_drain());
    TEST_ASSERT_EQUAL_INT(SW_OK, event_publish(EVT_OP_MODE_HOME_COMPLETED, 1U));
    TEST_ASSERT_EQUAL_INT(SW_OK, event_bus_drain());
    TEST_ASSERT_EQUAL_INT(0, s_recovery_completed_count);
}

/**
 * 锁存属非安全类，起步强制清掉。
 */
static void test_recovery_clears_latch_before_home(void)
{
    TEST_ASSERT_EQUAL_INT(SW_OK, event_bus_init());
    TEST_ASSERT_EQUAL_INT(SW_OK, alarm_registry_init());
    TEST_ASSERT_EQUAL_INT(SW_OK, alarm_registry_load_catalog(s_latch_catalog, 1U));
    TEST_ASSERT_EQUAL_INT(SW_OK, alarm_registry_trigger(TEST_LATCH_ALARM_CODE));
    TEST_ASSERT_TRUE(alarm_registry_is_active(TEST_LATCH_ALARM_CODE));
    device_ops_register(&s_device_ops);
    TEST_ASSERT_EQUAL_INT(SW_OK, recovery_coordinator_init());
    TEST_ASSERT_EQUAL_INT(SW_OK, event_subscribe(EVT_OP_MODE_RECOVERY_COMPLETED, on_recovery_completed));

    start_recover_via_command();
    TEST_ASSERT_EQUAL_INT(SW_OK, event_bus_drain());

    TEST_ASSERT_EQUAL_INT(1, s_recovery_completed_count);
    TEST_ASSERT_EQUAL_UINT32((uint32_t)RECOVERY_RESULT_STANDBY, s_recovery_result_param);
    TEST_ASSERT_FALSE(alarm_registry_is_active(TEST_LATCH_ALARM_CODE));
    TEST_ASSERT_EQUAL_INT(1, s_home_device_count);
}

/**
 * 机型清故障未就绪时先等，接点松开后再归位。
 */
static void test_recovery_waits_clear_alarms_before_home(void)
{
    s_device_ops.clear_alarms      = stub_clear_alarms;
    s_device_ops.poll_clear_alarms = stub_poll_clear_alarms;
    s_clear_poll_status            = DEVICE_CLEAR_ALARMS_PENDING;

    TEST_ASSERT_EQUAL_INT(SW_OK, event_bus_init());
    TEST_ASSERT_EQUAL_INT(SW_OK, alarm_registry_init());
    device_ops_register(&s_device_ops);
    TEST_ASSERT_EQUAL_INT(SW_OK, recovery_coordinator_init());
    TEST_ASSERT_EQUAL_INT(SW_OK, event_subscribe(EVT_OP_MODE_RECOVERY_COMPLETED, on_recovery_completed));

    start_recover_via_command();
    TEST_ASSERT_EQUAL_INT(SW_OK, event_bus_drain());

    TEST_ASSERT_EQUAL_INT(1, s_clear_alarms_count);
    TEST_ASSERT_EQUAL_INT(0, s_home_device_count);
    TEST_ASSERT_EQUAL_INT(0, s_recovery_completed_count);

    s_clear_poll_status = DEVICE_CLEAR_ALARMS_DONE;
    recovery_coordinator_poll();
    TEST_ASSERT_EQUAL_INT(1, s_home_device_count);
    TEST_ASSERT_EQUAL_INT(SW_OK, event_bus_drain());
    TEST_ASSERT_EQUAL_INT(1, s_recovery_completed_count);
    TEST_ASSERT_EQUAL_UINT32((uint32_t)RECOVERY_RESULT_STANDBY, s_recovery_result_param);
}

/**
 * 机型清故障超时则失败，不开归位。
 */
static void test_recovery_clear_alarms_failed_skips_home(void)
{
    s_device_ops.clear_alarms      = stub_clear_alarms;
    s_device_ops.poll_clear_alarms = stub_poll_clear_alarms;
    s_clear_poll_status            = DEVICE_CLEAR_ALARMS_FAILED;

    TEST_ASSERT_EQUAL_INT(SW_OK, event_bus_init());
    TEST_ASSERT_EQUAL_INT(SW_OK, alarm_registry_init());
    device_ops_register(&s_device_ops);
    TEST_ASSERT_EQUAL_INT(SW_OK, recovery_coordinator_init());
    TEST_ASSERT_EQUAL_INT(SW_OK, event_subscribe(EVT_OP_MODE_RECOVERY_COMPLETED, on_recovery_completed));

    start_recover_via_command();
    TEST_ASSERT_EQUAL_INT(SW_OK, event_bus_drain());

    TEST_ASSERT_EQUAL_INT(1, s_clear_alarms_count);
    TEST_ASSERT_EQUAL_INT(0, s_home_device_count);
    TEST_ASSERT_EQUAL_INT(1, s_recovery_completed_count);
    TEST_ASSERT_EQUAL_UINT32((uint32_t)RECOVERY_RESULT_FAILED, s_recovery_result_param);
}

/**
 * 清故障 DONE 只认领一次：并发再 poll 不得第二次 start_home。
 */
static void test_recovery_clear_done_claimed_once(void)
{
    s_device_ops.clear_alarms      = stub_clear_alarms;
    s_device_ops.poll_clear_alarms = stub_poll_clear_alarms;
    s_clear_poll_status            = DEVICE_CLEAR_ALARMS_PENDING;

    TEST_ASSERT_EQUAL_INT(SW_OK, event_bus_init());
    TEST_ASSERT_EQUAL_INT(SW_OK, alarm_registry_init());
    device_ops_register(&s_device_ops);
    TEST_ASSERT_EQUAL_INT(SW_OK, recovery_coordinator_init());
    TEST_ASSERT_EQUAL_INT(SW_OK, event_subscribe(EVT_OP_MODE_RECOVERY_COMPLETED, on_recovery_completed));

    start_recover_via_command();
    TEST_ASSERT_EQUAL_INT(SW_OK, event_bus_drain());
    TEST_ASSERT_EQUAL_INT(0, s_home_device_count);

    s_clear_poll_status = DEVICE_CLEAR_ALARMS_DONE;
    recovery_coordinator_poll();
    recovery_coordinator_poll();
    TEST_ASSERT_EQUAL_INT(1, s_home_device_count);
    TEST_ASSERT_EQUAL_INT(SW_OK, event_bus_drain());
    TEST_ASSERT_EQUAL_INT(1, s_recovery_completed_count);
    TEST_ASSERT_EQUAL_UINT32((uint32_t)RECOVERY_RESULT_STANDBY, s_recovery_result_param);
}

/**
 * 清故障等待中离开恢复后，迟到的 DONE 不开归位、不二次收尾。
 */
static void test_recovery_leave_during_clear_wait_skips_home(void)
{
    dev_cmd_t stop_all = dev_cmd_make_simple(DEV_CMD_STOP_ALL_OUTPUTS);

    s_device_ops.clear_alarms      = stub_clear_alarms;
    s_device_ops.poll_clear_alarms = stub_poll_clear_alarms;
    s_clear_poll_status            = DEVICE_CLEAR_ALARMS_PENDING;
    s_home_auto_complete           = 1;

    TEST_ASSERT_EQUAL_INT(SW_OK, event_bus_init());
    TEST_ASSERT_EQUAL_INT(SW_OK, alarm_registry_init());
    device_ops_register(&s_device_ops);
    TEST_ASSERT_EQUAL_INT(SW_OK, recovery_coordinator_init());
    TEST_ASSERT_EQUAL_INT(SW_OK, event_subscribe(EVT_OP_MODE_RECOVERY_COMPLETED, on_recovery_completed));

    start_recover_via_command();
    TEST_ASSERT_EQUAL_INT(SW_OK, event_bus_drain());
    TEST_ASSERT_EQUAL_INT(OP_MODE_RECOVERING, op_mode_get_current());
    TEST_ASSERT_EQUAL_INT(0, s_home_device_count);

    TEST_ASSERT_EQUAL_INT(OP_CMD_ALLOWED, op_mode_handle_command(&stop_all).verdict);
    TEST_ASSERT_EQUAL_INT(OP_MODE_STOPPED, op_mode_get_current());
    TEST_ASSERT_EQUAL_INT(SW_OK, event_bus_drain());

    s_clear_poll_status = DEVICE_CLEAR_ALARMS_DONE;
    recovery_coordinator_poll();
    TEST_ASSERT_EQUAL_INT(0, s_home_device_count);
    TEST_ASSERT_EQUAL_INT(0, s_recovery_completed_count);
    TEST_ASSERT_EQUAL_INT(OP_MODE_STOPPED, op_mode_get_current());
}

/**
 * 清故障等待中离开恢复后，迟到的 FAILED 不二次发布 RECOVERY_COMPLETED。
 */
static void test_recovery_leave_during_clear_wait_failed_no_complete(void)
{
    dev_cmd_t stop_all = dev_cmd_make_simple(DEV_CMD_STOP_ALL_OUTPUTS);

    s_device_ops.clear_alarms      = stub_clear_alarms;
    s_device_ops.poll_clear_alarms = stub_poll_clear_alarms;
    s_clear_poll_status            = DEVICE_CLEAR_ALARMS_PENDING;

    TEST_ASSERT_EQUAL_INT(SW_OK, event_bus_init());
    TEST_ASSERT_EQUAL_INT(SW_OK, alarm_registry_init());
    device_ops_register(&s_device_ops);
    TEST_ASSERT_EQUAL_INT(SW_OK, recovery_coordinator_init());
    TEST_ASSERT_EQUAL_INT(SW_OK, event_subscribe(EVT_OP_MODE_RECOVERY_COMPLETED, on_recovery_completed));

    start_recover_via_command();
    TEST_ASSERT_EQUAL_INT(SW_OK, event_bus_drain());

    TEST_ASSERT_EQUAL_INT(OP_CMD_ALLOWED, op_mode_handle_command(&stop_all).verdict);
    TEST_ASSERT_EQUAL_INT(SW_OK, event_bus_drain());

    s_clear_poll_status = DEVICE_CLEAR_ALARMS_FAILED;
    recovery_coordinator_poll();
    TEST_ASSERT_EQUAL_INT(0, s_home_device_count);
    TEST_ASSERT_EQUAL_INT(0, s_recovery_completed_count);
}

int main(void)
{
    UNITY_BEGIN();
    WDF_RUN_TEST(test_recovery_coordinator_publishes_completed_idle, "", "验证恢复完成后发布空闲状态事件");
    WDF_RUN_TEST(
        test_recovery_coordinator_keeps_exception_when_blocking_remains, "", "验证归位结束时阻断告警仍在则失败");
    WDF_RUN_TEST(test_recovery_resets_latch_cleared_during_home, "", "验证归位中条件消失的锁存结束后摘掉");
    WDF_RUN_TEST(test_recovery_safety_trigger_blocks_home, "", "验证安全触发还在时不开归位");
    WDF_RUN_TEST(test_recovery_clears_non_safety_before_home, "", "验证非安全类起步强制清除后可归位");
    WDF_RUN_TEST(test_recovery_critical_non_safety_starts_home, "", "验证非安全 CRITICAL 不挡恢复起步");
    WDF_RUN_TEST(test_recovery_resets_inactive_lockout_before_home, "", "验证回零前复位已失活的锁定报警");
    WDF_RUN_TEST(test_cutout_estop_release_does_not_run_abort_home, "", "验证急停释放的安全切断不执行中止回零");
    WDF_RUN_TEST(test_cutout_lockout_aborts_wash, "", "验证安全切断锁定中止洗车");
    WDF_RUN_TEST(test_abort_home_requested_runs_abort_home, "", "验证请求中止回零时执行中止回零操作");
    WDF_RUN_TEST(test_cutout_estop_on_aborts_wash_and_defers_stop, "", "验证安全切断急停开启中止洗车并延后停止");
    WDF_RUN_TEST(test_leave_recovering_ignores_late_home_completed, "", "验证离开恢复后忽略迟到归位完成");
    WDF_RUN_TEST(test_estop_during_pending_home_preempts_recovery, "", "验证归位等待中急停抢占恢复并切断");
    WDF_RUN_TEST(test_stop_all_during_pending_home_cuts_outputs, "", "验证归位等待中全停切断输出");
    WDF_RUN_TEST(test_recovery_clears_latch_before_home, "", "验证锁存起步即强制清除");
    WDF_RUN_TEST(test_recovery_waits_clear_alarms_before_home, "", "验证清故障未就绪时先等再归位");
    WDF_RUN_TEST(test_recovery_clear_alarms_failed_skips_home, "", "验证清故障超时不开归位");
    WDF_RUN_TEST(test_recovery_clear_done_claimed_once, "", "验证清故障完成只认领一次归位");
    WDF_RUN_TEST(test_recovery_leave_during_clear_wait_skips_home, "", "验证离开恢复后清故障完成不开归位");
    WDF_RUN_TEST(test_recovery_leave_during_clear_wait_failed_no_complete, "", "验证离开恢复后清故障失败不二次收尾");
    return UNITY_END();
}
