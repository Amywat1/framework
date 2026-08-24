/**
 * @file    test_telemetry_projection.c
 * @brief   telemetry snapshot/projection 鍗曞厓娴嬭瘯
 */

#include "application/telemetry_projection.h"
#include "common/event_types.h"
#include "common/sw_error.h"
#include "common/time_util.h"
#include "domain/op_mode/device_command.h"
#include "domain/op_mode/operational_mode.h"
#include "domain/ports/outbound/safety/safety_port.h"
#include "domain/safety/alarm_registry/alarm_registry.h"
#include "domain/telemetry/device_snapshot.h"
#include "domain/telemetry/device_snapshot_internal.h"
#include "runtime/event_bus/event_bus.h"
#include "runtime/event_bus/event_bus_config.h"
#include "runtime/ports/port_registry.h"
#include "runtime/scheduler/thread_registry.h"
#include "tests/stubs/test_wash_modes.h"
#include "wdf_test_spec.h"

static const alarm_def_t s_catalog[] = {
    {
     .code         = 201101U,
     .level        = ALARM_LEVEL_MAJOR,
     .clear        = ALARM_CLEAR_MANUAL_RESET,
     .reeval_group = ALARM_REEVAL_GROUP_NONE,
     .desc         = "blocking alarm",
     },
    {
     .code         = 201201U,
     .level        = ALARM_LEVEL_MAJOR,
     .clear        = ALARM_CLEAR_MANUAL_RESET,
     .reeval_group = ALARM_REEVAL_GROUP_NONE,
     .desc         = "blocking alarm 2",
     },
    {
     .code         = 201301U,
     .level        = ALARM_LEVEL_MAJOR,
     .clear        = ALARM_CLEAR_AUTO_STATIC,
     .reeval_group = ALARM_REEVAL_GROUP_NONE,
     .desc         = "auto static major",
     },
};

static sw_err_t failed_cutout(void)
{
    return SW_ERR_HW;
}

static bool inactive_estop(void)
{
    return false;
}

static bool non_estop_alarm(uint32_t code)
{
    (void)code;
    return false;
}

static void no_deferred_stop(void)
{
}

static const safety_ops_t s_failed_cutout_ops = {
    .cutout          = failed_cutout,
    .estop_is_active = inactive_estop,
    .alarm_is_estop  = non_estop_alarm,
    .deferred_stop   = no_deferred_stop,
};

static void publish_and_wait(event_type_t type, uint32_t param)
{
    TEST_ASSERT_EQUAL_INT(SW_OK, event_publish(type, param));
    TEST_ASSERT_EQUAL_INT(SW_OK, event_bus_drain());
}

void setUp(void)
{
    port_registry_safety_reset();
    /* 快照是进程级全局态，不复位则用例只在"先写后读"的写法下偶然成立，
     * 一旦有用例断言某子域的绝对值就会依赖执行顺序。 */
    device_snapshot_reset_for_test();

    /* 同理清空报警表：残留的活动报警会阻塞开洗并使静态态收敛到 STOPPED，
     * 断言 IDLE 的用例便只在"报警用例之后不执行"的顺序下成立。 */
    TEST_ASSERT_EQUAL_INT(SW_OK, alarm_registry_init());
}

void tearDown(void)
{
}

static void test_snapshot_direct_updates_are_read_back(void)
{
    operational_snapshot_t op = {
        .mode            = OP_MODE_IDLE,
        .service_enabled = true,
        .estop_active    = false,
    };
    safety_snapshot_t safety = {
        .posture         = SAFETY_POSTURE_LOCKOUT,
        .blocking_active = true,
    };

    device_snapshot_update_op(&op);
    device_snapshot_update_safety(&safety);
    device_snapshot_set_wash_mode(TEST_WASH_MODE_B);

    {
        operational_snapshot_t s = device_snapshot_get().op;
        TEST_ASSERT_TRUE(operational_snapshot_is_standby(s));
        TEST_ASSERT_FALSE(operational_snapshot_is_stopping(s));
    }
    TEST_ASSERT_TRUE(device_snapshot_get().safety.blocking_active);
    TEST_ASSERT_EQUAL_INT(TEST_WASH_MODE_B, device_snapshot_get().wash.mode);
}

static void test_wash_projection_tracks_session_started_event(void)
{
    time_util_init();
    TEST_ASSERT_EQUAL_INT(SW_OK, event_bus_init());
    TEST_ASSERT_EQUAL_INT(SW_OK, telemetry_projection_init());
    publish_and_wait(EVT_WASH_SESSION_STARTED, (uint32_t)TEST_WASH_MODE_B);
    TEST_ASSERT_EQUAL_INT(TEST_WASH_MODE_B, device_snapshot_get().wash.mode);
}

static void test_operational_projection_syncs_current_context(void)
{
    operational_snapshot_t snap;
    dev_cmd_t              recover_cmd = dev_cmd_make_simple(DEV_CMD_RECOVER);
    dev_cmd_t              stop_cmd    = dev_cmd_make_simple(DEV_CMD_STOP_OPERATION);

    time_util_init();
    TEST_ASSERT_EQUAL_INT(SW_OK, event_bus_init());
    TEST_ASSERT_EQUAL_INT(SW_OK, operational_mode_init());
    TEST_ASSERT_EQUAL_INT(SW_OK, telemetry_projection_init());
    /* 上电：STOPPED + 总开关开 */
    snap = device_snapshot_get().op;
    TEST_ASSERT_EQUAL_INT(OP_MODE_STOPPED, snap.mode);
    TEST_ASSERT_TRUE(snap.service_enabled);
    TEST_ASSERT_TRUE(operational_snapshot_is_stopping(snap));

    /* 恢复进 IDLE → 待机投影 */
    TEST_ASSERT_EQUAL_INT(OP_CMD_ALLOWED, op_mode_handle_command(&recover_cmd).verdict);
    op_mode_on_recovery_completed(RECOVERY_RESULT_IDLE);
    publish_and_wait(EVT_OP_MODE_CHANGED, 0U);
    snap = device_snapshot_get().op;
    TEST_ASSERT_EQUAL_INT(OP_MODE_IDLE, snap.mode);
    TEST_ASSERT_TRUE(snap.service_enabled);
    TEST_ASSERT_TRUE(operational_snapshot_is_standby(snap));

    /* 停运：STOPPED + 总开关关 */
    TEST_ASSERT_EQUAL_INT(OP_CMD_ALLOWED, op_mode_handle_command(&stop_cmd).verdict);
    TEST_ASSERT_EQUAL_INT(SW_OK, event_bus_drain());
    snap = device_snapshot_get().op;
    TEST_ASSERT_EQUAL_INT(OP_MODE_STOPPED, snap.mode);
    TEST_ASSERT_FALSE(snap.service_enabled);
    TEST_ASSERT_TRUE(operational_snapshot_is_stopping(snap));
    TEST_ASSERT_FALSE(operational_snapshot_is_standby(snap));
}

/* STOPPED 下置急停：无 MODE_CHANGED，须靠 CONTEXT_SYNC 刷新快照 estop_active */
static void test_estop_while_stopped_syncs_snapshot_flag(void)
{
    operational_snapshot_t snap;

    time_util_init();
    TEST_ASSERT_EQUAL_INT(SW_OK, event_bus_init());
    TEST_ASSERT_EQUAL_INT(SW_OK, operational_mode_init());
    TEST_ASSERT_EQUAL_INT(SW_OK, telemetry_projection_init());
    snap = device_snapshot_get().op;
    TEST_ASSERT_EQUAL_INT(OP_MODE_STOPPED, snap.mode);
    TEST_ASSERT_FALSE(snap.estop_active);

    op_mode_on_estop(true);
    TEST_ASSERT_EQUAL_INT(SW_OK, event_bus_drain());
    snap = device_snapshot_get().op;
    TEST_ASSERT_EQUAL_INT(OP_MODE_STOPPED, snap.mode);
    TEST_ASSERT_TRUE(snap.estop_active);
    TEST_ASSERT_TRUE(op_mode_is_estop_active());

    op_mode_on_estop(false);
    TEST_ASSERT_EQUAL_INT(SW_OK, event_bus_drain());
    snap = device_snapshot_get().op;
    TEST_ASSERT_FALSE(snap.estop_active);
}

static void test_safety_projection_refreshes_alarm_snapshot(void)
{
    safety_snapshot_t snap;

    time_util_init();
    TEST_ASSERT_EQUAL_INT(SW_OK, event_bus_init());
    TEST_ASSERT_EQUAL_INT(SW_OK, alarm_registry_init());
    TEST_ASSERT_EQUAL_INT(SW_OK, alarm_registry_load_catalog(s_catalog, 1U));
    TEST_ASSERT_EQUAL_INT(SW_OK, telemetry_projection_init());
    TEST_ASSERT_EQUAL_INT(SW_OK, alarm_registry_trigger(201101U));
    TEST_ASSERT_EQUAL_INT(SW_OK, event_bus_drain());

    snap = device_snapshot_get().safety;
    TEST_ASSERT_TRUE(snap.blocking_active);
    TEST_ASSERT_EQUAL_UINT(1U, snap.active_alarm_count);
    TEST_ASSERT_EQUAL_UINT(201101U, snap.top_alarm_code);
    TEST_ASSERT_TRUE(device_snapshot_get().safety.blocking_active);
}

static void test_explicit_rebuild_repairs_dropped_projection_event(void)
{
    operational_snapshot_t snap;
    dev_cmd_t              stop_cmd = dev_cmd_make_simple(DEV_CMD_STOP_OPERATION);

    time_util_init();
    thread_registry_reset_for_test();
    TEST_ASSERT_EQUAL_INT(SW_OK, event_bus_init());
    TEST_ASSERT_EQUAL_INT(SW_OK, operational_mode_init());
    TEST_ASSERT_EQUAL_INT(SW_OK, telemetry_projection_init());

    snap = device_snapshot_get().op;
    TEST_ASSERT_TRUE(snap.service_enabled);

    for (unsigned i = 0U; i < EVENT_BUS_QUEUE_SIZE; ++i) {
        TEST_ASSERT_EQUAL_INT(SW_OK, event_publish(EVT_CMD_ORDER, i));
    }
    TEST_ASSERT_EQUAL_INT(OP_CMD_ALLOWED, op_mode_handle_command(&stop_cmd).verdict);

    snap = device_snapshot_get().op;
    TEST_ASSERT_TRUE(snap.service_enabled);
    telemetry_projection_sync_all();
    snap = device_snapshot_get().op;
    TEST_ASSERT_FALSE(snap.service_enabled);
    TEST_ASSERT_EQUAL_INT(OP_MODE_STOPPED, snap.mode);
}

static void test_cutout_unconfirmed_forces_lockout_projection(void)
{
    safety_snapshot_t snap;

    TEST_ASSERT_EQUAL_INT(SW_OK, safety_port_register(&s_failed_cutout_ops));
    TEST_ASSERT_EQUAL_INT(SW_ERR_HW, safety_cutout_execute());
    telemetry_projection_sync_all();

    snap = device_snapshot_get().safety;
    TEST_ASSERT_EQUAL_INT(SAFETY_POSTURE_LOCKOUT, snap.posture);
    TEST_ASSERT_TRUE(snap.blocking_active);
    TEST_ASSERT_TRUE(snap.cutout_unconfirmed);
}

static void test_safety_projection_copies_session_journal(void)
{
    safety_snapshot_t snap;

    time_util_init();
    TEST_ASSERT_EQUAL_INT(SW_OK, event_bus_init());
    TEST_ASSERT_EQUAL_INT(SW_OK, alarm_registry_init());
    TEST_ASSERT_EQUAL_INT(SW_OK, alarm_registry_load_catalog(s_catalog, 3U));
    alarm_registry_on_wash_session_started();
    TEST_ASSERT_EQUAL_INT(SW_OK, alarm_registry_trigger(201101U));
    TEST_ASSERT_EQUAL_INT(SW_OK, alarm_registry_trigger(201201U));
    telemetry_projection_sync_all();

    snap = device_snapshot_get().safety;
    TEST_ASSERT_EQUAL_UINT(2U, snap.session_journal_count);
    TEST_ASSERT_EQUAL_UINT(201101U, snap.session_journal[0]);
    TEST_ASSERT_EQUAL_UINT(201201U, snap.session_journal[1]);
    TEST_ASSERT_EQUAL_UINT32(0U, snap.session_journal_dropped);
}

static void test_safety_projection_journal_dropped_visible(void)
{
    alarm_def_t       cat[ALARM_SESSION_JOURNAL_MAX + 1U];
    safety_snapshot_t snap;
    unsigned          i;

    for (i = 0U; i < (ALARM_SESSION_JOURNAL_MAX + 1U); ++i) {
        cat[i] = (alarm_def_t){
            .code         = ALARM_CODE_MAKE(ALM_C_SENSE, i, ALM_N_OVERLOAD),
            .level        = ALARM_LEVEL_MAJOR,
            .clear        = ALARM_CLEAR_AUTO_STATIC,
            .reeval_group = ALARM_REEVAL_GROUP_NONE,
            .desc         = "fill",
        };
    }

    time_util_init();
    TEST_ASSERT_EQUAL_INT(SW_OK, event_bus_init());
    TEST_ASSERT_EQUAL_INT(SW_OK, alarm_registry_init());
    TEST_ASSERT_EQUAL_INT(SW_OK, alarm_registry_load_catalog(cat, ALARM_SESSION_JOURNAL_MAX + 1U));
    alarm_registry_on_wash_session_started();
    for (i = 0U; i < (ALARM_SESSION_JOURNAL_MAX + 1U); ++i) {
        TEST_ASSERT_EQUAL_INT(SW_OK, alarm_registry_trigger(ALARM_CODE_MAKE(ALM_C_SENSE, i, ALM_N_OVERLOAD)));
    }
    telemetry_projection_sync_all();

    snap = device_snapshot_get().safety;
    TEST_ASSERT_EQUAL_UINT(ALARM_SESSION_JOURNAL_MAX, snap.session_journal_count);
    TEST_ASSERT_TRUE(snap.session_journal_dropped >= 1U);
}

static void test_cleared_auto_static_keeps_journal_without_blocking(void)
{
    safety_snapshot_t snap;

    time_util_init();
    TEST_ASSERT_EQUAL_INT(SW_OK, event_bus_init());
    TEST_ASSERT_EQUAL_INT(SW_OK, alarm_registry_init());
    TEST_ASSERT_EQUAL_INT(SW_OK, alarm_registry_load_catalog(s_catalog, 3U));
    alarm_registry_on_wash_session_started();
    TEST_ASSERT_EQUAL_INT(SW_OK, alarm_registry_trigger(201301U));
    TEST_ASSERT_EQUAL_INT(SW_OK, alarm_registry_clear(201301U));
    telemetry_projection_sync_all();

    snap = device_snapshot_get().safety;
    TEST_ASSERT_FALSE(snap.blocking_active);
    TEST_ASSERT_FALSE(alarm_registry_has_blocking_active());
    TEST_ASSERT_EQUAL_UINT(1U, snap.session_journal_count);
    TEST_ASSERT_EQUAL_UINT(201301U, snap.session_journal[0]);
}

int main(void)
{
    UNITY_BEGIN();

    WDF_RUN_TEST(test_snapshot_direct_updates_are_read_back, "", "验证直接更新快照后可以读取新值");
    WDF_RUN_TEST(test_wash_projection_tracks_session_started_event, "", "验证洗车投影跟踪会话已启动事件");
    WDF_RUN_TEST(test_operational_projection_syncs_current_context, "", "验证运行状态投影同步当前上下文");
    WDF_RUN_TEST(test_estop_while_stopped_syncs_snapshot_flag, "", "验证已停止时急停仍刷新快照旗标");
    WDF_RUN_TEST(test_safety_projection_refreshes_alarm_snapshot, "", "验证安全投影刷新报警快照");
    WDF_RUN_TEST(test_explicit_rebuild_repairs_dropped_projection_event, "", "验证事件丢失后显式重建修复投影");
    WDF_RUN_TEST(test_cutout_unconfirmed_forces_lockout_projection, "SAFE-11", "验证切断未确认强制安全投影锁定");
    WDF_RUN_TEST(test_safety_projection_copies_session_journal, "ALRM-21", "验证安全快照包含会话 journal");
    WDF_RUN_TEST(test_safety_projection_journal_dropped_visible, "ALRM-21", "验证快照可见 journal 丢弃计数");
    WDF_RUN_TEST(test_cleared_auto_static_keeps_journal_without_blocking,
                 "ALRM-21",
                 "验证已清除 AUTO_STATIC 仍在 journal 且不阻塞");

    return UNITY_END();
}
