/**
 * @file    test_operational_mode.c
 * @brief   operational_mode 命令矩阵与模式转移单元测试
 */

#include "common/sw_error.h"
#include "common/time_util.h"
#include "runtime/event_bus/event_bus.h"
#include "domain/op_mode/device_command.h"
#include "domain/op_mode/op_mode_types.h"
#include "domain/op_mode/operational_mode.h"
#include "domain/ports/outbound/device/device_ops_port.h"
#include "domain/ports/outbound/safety/safety_port.h"
#include "domain/safety/alarm_registry/alarm_registry.h"
#include "domain/safety/model/alarm_types.h"
#include "runtime/ports/port_registry.h"
#include "tests/stubs/test_wash_modes.h"
#include "wdf_test_spec.h"

#define TEST_ALARM_BLOCKING ALARM_CODE_MAKE(ALM_C_SENSE, 1U, ALM_N_SIG_ERR)

static const alarm_def_t s_catalog[] = {
    {
     .code         = TEST_ALARM_BLOCKING,
     .level        = ALARM_LEVEL_MAJOR,
     .clear        = ALARM_CLEAR_MANUAL_RESET,
     .reeval_group = ALARM_REEVAL_GROUP_NONE,
     .desc         = "test blocking",
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

static void load_alarm_catalog(void)
{
    TEST_ASSERT_EQUAL_INT(SW_OK, alarm_registry_load_catalog(s_catalog, 1U));
}

/* 辅助：从 STOPPED 经统一 recovery 路径进入 IDLE */
static void enter_idle(void)
{
    dev_cmd_t          cmd = dev_cmd_make_simple(DEV_CMD_RECOVER);
    dev_cmd_decision_t d   = op_mode_handle_command(&cmd);

    TEST_ASSERT_EQUAL_INT(OP_CMD_ALLOWED, d.verdict);
    TEST_ASSERT_EQUAL_INT(OP_MODE_RECOVERING, op_mode_get_current());
    op_mode_on_recovery_completed(RECOVERY_RESULT_IDLE);
    TEST_ASSERT_EQUAL_INT(OP_MODE_IDLE, op_mode_get_current());
}

void setUp(void)
{
    port_registry_safety_reset();
    device_ops_register(NULL);
    time_util_init();
    TEST_ASSERT_EQUAL_INT(SW_OK, event_bus_init());
    TEST_ASSERT_EQUAL_INT(SW_OK, alarm_registry_init());
    load_alarm_catalog();
    TEST_ASSERT_EQUAL_INT(SW_OK, operational_mode_init());
}

void tearDown(void)
{
    device_ops_register(NULL);
}

/* 上电后应处于 STOPPED，service_enabled=true，非待机 */
static void test_init_stopped_and_service_enabled(void)
{
    TEST_ASSERT_EQUAL_INT(OP_MODE_STOPPED, op_mode_get_current());
    TEST_ASSERT_TRUE(op_mode_is_service_enabled());
    TEST_ASSERT_FALSE(op_mode_is_standby());
    TEST_ASSERT_FALSE(op_mode_is_estop_active());
}

/* STOPPED → RECOVER → RECOVERING → IDLE */
static void test_recover_from_stopped_enters_idle(void)
{
    enter_idle();
    TEST_ASSERT_TRUE(op_mode_is_standby());
}

/* 恢复失败 → STOPPED */
static void test_recover_home_failure_enters_stopped(void)
{
    dev_cmd_t          cmd = dev_cmd_make_simple(DEV_CMD_RECOVER);
    dev_cmd_decision_t d   = op_mode_handle_command(&cmd);

    TEST_ASSERT_EQUAL_INT(OP_CMD_ALLOWED, d.verdict);
    op_mode_on_recovery_completed(RECOVERY_RESULT_FAILED);
    TEST_ASSERT_EQUAL_INT(OP_MODE_STOPPED, op_mode_get_current());
}

static void test_cutout_unconfirmed_blocks_recovery(void)
{
    dev_cmd_t          cmd = dev_cmd_make_simple(DEV_CMD_RECOVER);
    dev_cmd_decision_t decision;

    TEST_ASSERT_EQUAL_INT(SW_OK, safety_port_register(&s_failed_cutout_ops));
    TEST_ASSERT_EQUAL_INT(SW_ERR_HW, safety_cutout_execute());
    TEST_ASSERT_TRUE(safety_cutout_is_unconfirmed());

    decision = op_mode_handle_command(&cmd);
    TEST_ASSERT_EQUAL_INT(OP_CMD_DENIED, decision.verdict);
    TEST_ASSERT_EQUAL_INT(OP_REJECT_CUTOUT_UNCONFIRMED, decision.reason);
    TEST_ASSERT_EQUAL_INT(OP_MODE_STOPPED, op_mode_get_current());
}

/* STOPPED 下阻塞告警保持 STOPPED（故障由旗标表达）*/
static void test_blocking_alarm_from_stopped_stays_stopped(void)
{
    TEST_ASSERT_EQUAL_INT(SW_OK, alarm_registry_trigger(TEST_ALARM_BLOCKING));
    op_mode_on_blocking_alarm();
    TEST_ASSERT_EQUAL_INT(OP_MODE_STOPPED, op_mode_get_current());
}

/* IDLE 下阻塞告警 → STOPPED */
static void test_blocking_alarm_from_idle_enters_stopped(void)
{
    enter_idle();
    TEST_ASSERT_EQUAL_INT(SW_OK, alarm_registry_trigger(TEST_ALARM_BLOCKING));
    op_mode_on_blocking_alarm();
    TEST_ASSERT_EQUAL_INT(OP_MODE_STOPPED, op_mode_get_current());
}

/* RECOVERING 不立即中断；完成时由 recovery 结果落入 STOPPED */
static void test_blocking_alarm_during_home_lands_stopped(void)
{
    dev_cmd_t cmd = dev_cmd_make_simple(DEV_CMD_RECOVER);

    (void)op_mode_handle_command(&cmd);
    TEST_ASSERT_EQUAL_INT(OP_MODE_RECOVERING, op_mode_get_current());
    TEST_ASSERT_EQUAL_INT(SW_OK, alarm_registry_trigger(TEST_ALARM_BLOCKING));
    op_mode_on_blocking_alarm();
    TEST_ASSERT_EQUAL_INT(OP_MODE_RECOVERING, op_mode_get_current());

    op_mode_on_recovery_completed(RECOVERY_RESULT_FAILED);
    TEST_ASSERT_EQUAL_INT(OP_MODE_STOPPED, op_mode_get_current());
}

/* 停运后（STOPPED，总开关仍开）可 RECOVER；关总开关后才拒绝 */
static void test_recover_denied_when_service_disabled(void)
{
    dev_cmd_t recover_cmd = dev_cmd_make_simple(DEV_CMD_RECOVER);
    dev_cmd_t stop_cmd    = dev_cmd_make_simple(DEV_CMD_STOP_ALL_OUTPUTS);
    dev_cmd_t off_cmd     = dev_cmd_make_set_service(false);

    enter_idle();
    (void)op_mode_handle_command(&stop_cmd); /* STOP_ALL → STOPPED, service 仍开 */
    TEST_ASSERT_EQUAL_INT(OP_MODE_STOPPED, op_mode_get_current());
    TEST_ASSERT_TRUE(op_mode_is_service_enabled());
    TEST_ASSERT_EQUAL_INT(OP_CMD_ALLOWED, op_mode_handle_command(&recover_cmd).verdict);

    op_mode_on_recovery_completed(RECOVERY_RESULT_IDLE);
    TEST_ASSERT_EQUAL_INT(OP_MODE_IDLE, op_mode_get_current());

    (void)op_mode_handle_command(&off_cmd); /* → STOPPED, service=off */
    TEST_ASSERT_EQUAL_INT(OP_MODE_STOPPED, op_mode_get_current());
    TEST_ASSERT_FALSE(op_mode_is_service_enabled());
    TEST_ASSERT_EQUAL_INT(OP_REJECT_SERVICE_DISABLED, op_mode_handle_command(&recover_cmd).reason);
}

/* IDLE 下重复 RECOVER 幂等成功且不产生副作用 */
static void test_recover_in_idle_is_idempotent(void)
{
    dev_cmd_t          cmd = dev_cmd_make_simple(DEV_CMD_RECOVER);
    dev_cmd_decision_t d;

    enter_idle();
    d = op_mode_handle_command(&cmd);
    TEST_ASSERT_EQUAL_INT(OP_CMD_ALLOWED, d.verdict);
    TEST_ASSERT_EQUAL_INT(OP_MODE_IDLE, op_mode_get_current());
}

/* IDLE 可以接单 */
static void test_start_wash_allowed_in_idle(void)
{
    dev_cmd_t          cmd = dev_cmd_make_start_wash(TEST_WASH_MODE_A);
    dev_cmd_decision_t d;

    enter_idle();
    d = op_mode_handle_command(&cmd);
    TEST_ASSERT_EQUAL_INT(OP_CMD_ALLOWED, d.verdict);
    TEST_ASSERT_EQUAL_INT(OP_MODE_IDLE, op_mode_get_current());
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

/* STOPPED 下也可全停（幂等；无需先 RECOVER 进 IDLE）*/
static void test_stop_all_allowed_from_stopped(void)
{
    dev_cmd_t cmd = dev_cmd_make_simple(DEV_CMD_STOP_ALL_OUTPUTS);

    TEST_ASSERT_EQUAL_INT(OP_MODE_STOPPED, op_mode_get_current());
    TEST_ASSERT_TRUE(op_mode_is_service_enabled());
    TEST_ASSERT_EQUAL_INT(OP_CMD_ALLOWED, op_mode_handle_command(&cmd).verdict);
    TEST_ASSERT_EQUAL_INT(OP_MODE_STOPPED, op_mode_get_current());
    TEST_ASSERT_TRUE(op_mode_is_service_enabled());
}

/* SET_SERVICE：任意非 INIT 态允许开启；已开则幂等；不改模式 */
static void test_set_service_enable_is_idempotent(void)
{
    dev_cmd_t stop_cmd = dev_cmd_make_simple(DEV_CMD_STOP_ALL_OUTPUTS);
    dev_cmd_t on_cmd   = dev_cmd_make_set_service(true);
    dev_cmd_t off_cmd  = dev_cmd_make_set_service(false);

    enter_idle();
    TEST_ASSERT_EQUAL_INT(OP_CMD_ALLOWED, op_mode_handle_command(&on_cmd).verdict);
    TEST_ASSERT_TRUE(op_mode_is_service_enabled());
    TEST_ASSERT_EQUAL_INT(OP_MODE_IDLE, op_mode_get_current());

    (void)op_mode_handle_command(&stop_cmd);
    TEST_ASSERT_EQUAL_INT(OP_MODE_STOPPED, op_mode_get_current());
    TEST_ASSERT_TRUE(op_mode_is_service_enabled());
    TEST_ASSERT_EQUAL_INT(OP_CMD_ALLOWED, op_mode_handle_command(&off_cmd).verdict);
    TEST_ASSERT_FALSE(op_mode_is_service_enabled());
    TEST_ASSERT_EQUAL_INT(OP_MODE_STOPPED, op_mode_get_current());
    TEST_ASSERT_EQUAL_INT(OP_CMD_ALLOWED, op_mode_handle_command(&on_cmd).verdict);
    TEST_ASSERT_TRUE(op_mode_is_service_enabled());
    TEST_ASSERT_EQUAL_INT(OP_MODE_STOPPED, op_mode_get_current());
    TEST_ASSERT_EQUAL_INT(OP_CMD_ALLOWED, op_mode_handle_command(&on_cmd).verdict);
}

/* SET_SERVICE 关闭：IDLE 因不变量落到 STOPPED */
static void test_set_service_disable_from_idle_enters_stopped(void)
{
    dev_cmd_t off_cmd = dev_cmd_make_set_service(false);

    enter_idle();
    TEST_ASSERT_EQUAL_INT(OP_CMD_ALLOWED, op_mode_handle_command(&off_cmd).verdict);
    TEST_ASSERT_EQUAL_INT(OP_MODE_STOPPED, op_mode_get_current());
    TEST_ASSERT_FALSE(op_mode_is_service_enabled());
}

/* 洗车中拒绝关闭总开关；开启仍允许 */
static void test_set_service_disable_denied_while_washing(void)
{
    dev_cmd_t off_cmd = dev_cmd_make_set_service(false);
    dev_cmd_t on_cmd  = dev_cmd_make_set_service(true);

    enter_idle();
    op_mode_on_wash_session_started();
    TEST_ASSERT_EQUAL_INT(OP_MODE_WASHING, op_mode_get_current());
    TEST_ASSERT_EQUAL_INT(OP_REJECT_WRONG_MODE, op_mode_handle_command(&off_cmd).reason);
    TEST_ASSERT_TRUE(op_mode_is_service_enabled());
    TEST_ASSERT_EQUAL_INT(OP_CMD_ALLOWED, op_mode_handle_command(&on_cmd).verdict);
    TEST_ASSERT_EQUAL_INT(OP_MODE_WASHING, op_mode_get_current());
}

/* 全停后离开 IDLE，START_WASH 因模式拒绝（非 SERVICE_DISABLED） */
static void test_start_wash_wrong_mode_after_stop_all(void)
{
    dev_cmd_t stop_cmd = dev_cmd_make_simple(DEV_CMD_STOP_ALL_OUTPUTS);
    dev_cmd_t cmd      = dev_cmd_make_start_wash(TEST_WASH_MODE_A);

    enter_idle();
    (void)op_mode_handle_command(&stop_cmd);
    TEST_ASSERT_EQUAL_INT(OP_MODE_STOPPED, op_mode_get_current());
    TEST_ASSERT_EQUAL_INT(OP_REJECT_WRONG_MODE, op_mode_handle_command(&cmd).reason);
}

/* 有阻塞告警时拒绝 START_WASH */
static void test_start_wash_denied_with_blocking_alarm(void)
{
    dev_cmd_t cmd = dev_cmd_make_start_wash(TEST_WASH_MODE_A);

    load_alarm_catalog();
    enter_idle();
    TEST_ASSERT_EQUAL_INT(SW_OK, alarm_registry_trigger(TEST_ALARM_BLOCKING));
    TEST_ASSERT_EQUAL_INT(OP_REJECT_WRONG_MODE, op_mode_handle_command(&cmd).reason);
}

static bool stub_wash_entry_not_ready(void)
{
    return false;
}

/* 机型准入门未就绪时拒绝 START_WASH */
static void test_start_wash_denied_when_vehicle_not_ready(void)
{
    static const device_ops_t s_ops = {
        .is_wash_entry_ready = stub_wash_entry_not_ready,
    };
    dev_cmd_t cmd = dev_cmd_make_start_wash(TEST_WASH_MODE_A);

    device_ops_register(&s_ops);
    enter_idle();
    TEST_ASSERT_EQUAL_INT(OP_REJECT_VEHICLE_NOT_READY, op_mode_handle_command(&cmd).reason);
}

/* 急停激活时 → STOPPED，RECOVER 被拒绝 */
static void test_estop_blocks_recover(void)
{
    dev_cmd_t cmd = dev_cmd_make_simple(DEV_CMD_RECOVER);

    op_mode_on_estop(true);
    TEST_ASSERT_TRUE(op_mode_is_estop_active());
    TEST_ASSERT_EQUAL_INT(OP_MODE_STOPPED, op_mode_get_current());
    TEST_ASSERT_EQUAL_INT(OP_REJECT_ESTOP_ACTIVE, op_mode_handle_command(&cmd).reason);
}

/* 急停解除不自动进 IDLE，模式保持 STOPPED */
static void test_estop_release_stays_stopped(void)
{
    op_mode_on_estop(true);
    op_mode_on_estop(false);
    TEST_ASSERT_FALSE(op_mode_is_estop_active());
    TEST_ASSERT_EQUAL_INT(OP_MODE_STOPPED, op_mode_get_current());
}

/* STOPPED 下 RECOVER 进入恢复流程（含故障旗标场景）*/
static void test_recover_from_stopped_enters_recovering(void)
{
    dev_cmd_t          cmd = dev_cmd_make_simple(DEV_CMD_RECOVER);
    dev_cmd_decision_t d;

    op_mode_on_blocking_alarm();
    d = op_mode_handle_command(&cmd);
    TEST_ASSERT_EQUAL_INT(OP_CMD_ALLOWED, d.verdict);
    TEST_ASSERT_EQUAL_INT(OP_MODE_RECOVERING, op_mode_get_current());
}

/* 总开关关闭时 RECOVER 被拒绝 */
static void test_recover_denied_when_service_disabled_after_critical(void)
{
    dev_cmd_t recover_cmd = dev_cmd_make_simple(DEV_CMD_RECOVER);
    dev_cmd_t off_cmd     = dev_cmd_make_set_service(false);

    enter_idle();
    (void)op_mode_handle_command(&off_cmd);
    op_mode_on_blocking_alarm();
    TEST_ASSERT_EQUAL_INT(OP_MODE_STOPPED, op_mode_get_current());
    TEST_ASSERT_EQUAL_INT(OP_REJECT_SERVICE_DISABLED, op_mode_handle_command(&recover_cmd).reason);
}

/* 洗车完整生命周期 */
static void test_wash_session_lifecycle(void)
{
    enter_idle();

    op_mode_on_wash_session_started();
    TEST_ASSERT_EQUAL_INT(OP_MODE_WASHING, op_mode_get_current());

    op_mode_on_wash_session_completed();
    TEST_ASSERT_EQUAL_INT(OP_MODE_WASH_DONE, op_mode_get_current());

    op_mode_on_wash_customer_gone();
    TEST_ASSERT_EQUAL_INT(OP_MODE_IDLE, op_mode_get_current());

    op_mode_on_wash_session_started();
    op_mode_on_wash_session_aborted(WASH_ABORT_CRITICAL);
    TEST_ASSERT_EQUAL_INT(OP_MODE_ABORT_HOMING, op_mode_get_current());

    op_mode_on_home_done();
    TEST_ASSERT_EQUAL_INT(OP_MODE_STOPPED, op_mode_get_current());
}

/* 洗车正常结束时仍有 MAJOR+ → STOPPED（洗后评估）*/
static void test_wash_done_with_blocking_enters_stopped(void)
{
    load_alarm_catalog();
    enter_idle();

    op_mode_on_wash_session_started();
    TEST_ASSERT_EQUAL_INT(OP_MODE_WASHING, op_mode_get_current());

    TEST_ASSERT_EQUAL_INT(SW_OK, alarm_registry_trigger(TEST_ALARM_BLOCKING));
    op_mode_on_wash_session_completed();
    TEST_ASSERT_EQUAL_INT(OP_MODE_STOPPED, op_mode_get_current());
}

/* 洗车中 AUTO_STATIC MAJOR 已清除：journal 可能非空，但仍以当前 blocking 为准 → WASH_DONE */
static void test_wash_done_after_cleared_auto_static_enters_wash_done(void)
{
    alarm_def_t cat = {
        .code         = ALARM_CODE_MAKE(ALM_C_SENSE, 2U, ALM_N_SIG_ERR),
        .level        = ALARM_LEVEL_MAJOR,
        .clear        = ALARM_CLEAR_AUTO_STATIC,
        .reeval_group = ALARM_REEVAL_GROUP_NONE,
        .desc         = "auto major",
    };

    TEST_ASSERT_EQUAL_INT(SW_OK, alarm_registry_load_catalog(&cat, 1U));
    enter_idle();

    op_mode_on_wash_session_started();
    TEST_ASSERT_EQUAL_INT(OP_MODE_WASHING, op_mode_get_current());

    TEST_ASSERT_EQUAL_INT(SW_OK, alarm_registry_trigger(cat.code));
    TEST_ASSERT_EQUAL_INT(SW_OK, alarm_registry_clear(cat.code));
    TEST_ASSERT_FALSE(alarm_registry_has_blocking_active());

    op_mode_on_wash_session_completed();
    TEST_ASSERT_EQUAL_INT(OP_MODE_WASH_DONE, op_mode_get_current());
}

/* STOPPED 允许手动点动 */
static void test_manual_actuator_allowed_in_stopped(void)
{
    dev_cmd_t          cmd = dev_cmd_make_manual(7U, -1);
    dev_cmd_decision_t d   = op_mode_handle_command(&cmd);

    TEST_ASSERT_EQUAL_INT(OP_CMD_ALLOWED, d.verdict);
}

/* 急停时手动点动被拒绝 */
static void test_manual_actuator_denied_with_estop(void)
{
    dev_cmd_t cmd;

    op_mode_on_estop(true);
    TEST_ASSERT_EQUAL_INT(OP_MODE_STOPPED, op_mode_get_current());

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

/* 自检：失败仍 → STOPPED（故障由旗标表达）*/
static void test_self_check_from_stopped_fail_returns_stopped(void)
{
    dev_cmd_t cmd = dev_cmd_make_simple(DEV_CMD_START_SELF_CHECK);

    (void)op_mode_handle_command(&cmd);
    op_mode_on_self_check_completed(true);
    TEST_ASSERT_EQUAL_INT(OP_MODE_STOPPED, op_mode_get_current());
}

/* 自检：急停激活时拒绝启动 */
static void test_self_check_denied_when_estop(void)
{
    dev_cmd_t cmd = dev_cmd_make_simple(DEV_CMD_START_SELF_CHECK);

    op_mode_on_estop(true);
    TEST_ASSERT_EQUAL_INT(OP_REJECT_ESTOP_ACTIVE, op_mode_handle_command(&cmd).reason);
}

/* STOP_ALL：IDLE → STOPPED，保留总开关 */
static void test_stop_all_from_idle_enters_stopped(void)
{
    dev_cmd_t          cmd = dev_cmd_make_simple(DEV_CMD_STOP_ALL_OUTPUTS);
    dev_cmd_decision_t d;

    enter_idle();
    d = op_mode_handle_command(&cmd);
    TEST_ASSERT_EQUAL_INT(OP_CMD_ALLOWED, d.verdict);
    TEST_ASSERT_EQUAL_INT(OP_MODE_STOPPED, op_mode_get_current());
    TEST_ASSERT_TRUE(op_mode_is_service_enabled());
}

/* STOP_ALL：WASHING 先切 STOPPED，副作用含 abort；随后 abort 钩子不进清障 */
static void test_stop_all_from_washing_skips_abort_homing(void)
{
    dev_cmd_t          cmd = dev_cmd_make_simple(DEV_CMD_STOP_ALL_OUTPUTS);
    dev_cmd_decision_t d;

    enter_idle();
    op_mode_on_wash_session_started();
    TEST_ASSERT_EQUAL_INT(OP_MODE_WASHING, op_mode_get_current());

    d = op_mode_handle_command(&cmd);
    TEST_ASSERT_EQUAL_INT(OP_CMD_ALLOWED, d.verdict);
    TEST_ASSERT_EQUAL_INT(OP_MODE_WASHING, d.mode_before); /* 与裁决同锁快照，供 router abort */
    TEST_ASSERT_EQUAL_INT(OP_MODE_STOPPED, op_mode_get_current());

    op_mode_on_wash_session_aborted(WASH_ABORT_STOP_ALL);
    TEST_ASSERT_EQUAL_INT(OP_MODE_STOPPED, op_mode_get_current());
}

/* STOP_ALL：RECOVERING → STOPPED */
static void test_stop_all_from_recovering_enters_stopped(void)
{
    dev_cmd_t recover  = dev_cmd_make_simple(DEV_CMD_RECOVER);
    dev_cmd_t stop_all = dev_cmd_make_simple(DEV_CMD_STOP_ALL_OUTPUTS);

    (void)op_mode_handle_command(&recover);
    TEST_ASSERT_EQUAL_INT(OP_MODE_RECOVERING, op_mode_get_current());
    TEST_ASSERT_EQUAL_INT(OP_CMD_ALLOWED, op_mode_handle_command(&stop_all).verdict);
    TEST_ASSERT_EQUAL_INT(OP_MODE_STOPPED, op_mode_get_current());
}

/*
 * MODE-01：命令 × 8 状态穷举矩阵（与设计文档转录表一致）
 * D=DENIED A=ALLOWED C=CONDITIONAL；列序 INIT..RECOVERING
 */
static void test_cmd_matrix_exhaustive_64(void)
{
    /* clang-format off */
    static const op_cmd_perm_t expect[DEV_CMD_MAX][OP_MODE_RECOVERING + 1] = {
        [DEV_CMD_START_WASH] = {
            OP_CMD_PERM_DENIED, OP_CMD_PERM_DENIED, OP_CMD_PERM_CONDITIONAL, OP_CMD_PERM_DENIED,
            OP_CMD_PERM_DENIED, OP_CMD_PERM_DENIED, OP_CMD_PERM_DENIED, OP_CMD_PERM_DENIED,
        },
        [DEV_CMD_STOP_WASH] = {
            OP_CMD_PERM_DENIED, OP_CMD_PERM_DENIED, OP_CMD_PERM_DENIED, OP_CMD_PERM_ALLOWED,
            OP_CMD_PERM_DENIED, OP_CMD_PERM_DENIED, OP_CMD_PERM_DENIED, OP_CMD_PERM_DENIED,
        },
        [DEV_CMD_SET_SERVICE] = {
            OP_CMD_PERM_DENIED, OP_CMD_PERM_ALLOWED, OP_CMD_PERM_ALLOWED, OP_CMD_PERM_ALLOWED,
            OP_CMD_PERM_ALLOWED, OP_CMD_PERM_ALLOWED, OP_CMD_PERM_ALLOWED, OP_CMD_PERM_ALLOWED,
        },
        [DEV_CMD_MANUAL_ACTUATOR] = {
            OP_CMD_PERM_DENIED, OP_CMD_PERM_CONDITIONAL, OP_CMD_PERM_DENIED, OP_CMD_PERM_DENIED,
            OP_CMD_PERM_DENIED, OP_CMD_PERM_DENIED, OP_CMD_PERM_DENIED, OP_CMD_PERM_DENIED,
        },
        [DEV_CMD_START_SELF_CHECK] = {
            OP_CMD_PERM_DENIED, OP_CMD_PERM_CONDITIONAL, OP_CMD_PERM_DENIED, OP_CMD_PERM_DENIED,
            OP_CMD_PERM_DENIED, OP_CMD_PERM_DENIED, OP_CMD_PERM_DENIED, OP_CMD_PERM_DENIED,
        },
        [DEV_CMD_RECOVER] = {
            OP_CMD_PERM_DENIED, OP_CMD_PERM_CONDITIONAL, OP_CMD_PERM_ALLOWED, OP_CMD_PERM_DENIED,
            OP_CMD_PERM_DENIED, OP_CMD_PERM_DENIED, OP_CMD_PERM_DENIED, OP_CMD_PERM_DENIED,
        },
        [DEV_CMD_STOP_ALL_OUTPUTS] = {
            OP_CMD_PERM_DENIED, OP_CMD_PERM_ALLOWED, OP_CMD_PERM_ALLOWED, OP_CMD_PERM_ALLOWED,
            OP_CMD_PERM_ALLOWED, OP_CMD_PERM_ALLOWED, OP_CMD_PERM_ALLOWED, OP_CMD_PERM_ALLOWED,
        },
        [DEV_CMD_CLOUD_SYNC] = {
            OP_CMD_PERM_ALLOWED, OP_CMD_PERM_ALLOWED, OP_CMD_PERM_ALLOWED, OP_CMD_PERM_ALLOWED,
            OP_CMD_PERM_ALLOWED, OP_CMD_PERM_ALLOWED, OP_CMD_PERM_ALLOWED, OP_CMD_PERM_ALLOWED,
        },
    };
    /* clang-format on */
    dev_cmd_kind_t     kind;
    operational_mode_t mode;

    for (kind = (dev_cmd_kind_t)(DEV_CMD_NONE + 1); kind < DEV_CMD_MAX; kind++) {
        for (mode = OP_MODE_INIT; mode <= OP_MODE_RECOVERING; mode++) {
            TEST_ASSERT_EQUAL_INT(expect[kind][mode], op_mode_cmd_matrix_perm(kind, mode));
        }
    }
}

int main(void)
{
    UNITY_BEGIN();

    WDF_RUN_TEST(test_init_stopped_and_service_enabled, "", "验证初始化停止模式并服务启用");
    WDF_RUN_TEST(test_recover_from_stopped_enters_idle, "", "验证恢复从停止模式进入空闲模式");
    WDF_RUN_TEST(test_recover_home_failure_enters_stopped, "", "验证恢复回零失败进入停止模式");
    WDF_RUN_TEST(test_cutout_unconfirmed_blocks_recovery, "SAFE-11", "验证切断未确认时拒绝恢复");
    WDF_RUN_TEST(test_blocking_alarm_from_stopped_stays_stopped, "", "验证停止模式阻断报警仍为停止");
    WDF_RUN_TEST(test_blocking_alarm_from_idle_enters_stopped, "", "验证空闲模式阻断报警进入停止");
    WDF_RUN_TEST(test_blocking_alarm_during_home_lands_stopped, "", "验证回零期间阻断报警完成后进入停止");
    WDF_RUN_TEST(test_recover_denied_when_service_disabled, "", "验证恢复被拒绝时服务禁用");
    WDF_RUN_TEST(test_recover_in_idle_is_idempotent, "", "验证恢复在空闲模式为幂等");
    WDF_RUN_TEST(test_start_wash_allowed_in_idle, "", "验证启动洗车被允许在空闲模式");
    WDF_RUN_TEST(test_stop_wash_denied_in_idle, "", "验证停止洗车被拒绝在空闲模式");
    WDF_RUN_TEST(test_stop_all_allowed_from_stopped, "", "验证停止模式下允许全停且总开关保持");
    WDF_RUN_TEST(test_set_service_enable_is_idempotent, "", "验证开启总开关幂等且不改模式");
    WDF_RUN_TEST(test_set_service_disable_from_idle_enters_stopped, "", "验证待机关总开关落入停机");
    WDF_RUN_TEST(test_set_service_disable_denied_while_washing, "", "验证洗车中拒绝关闭总开关");
    WDF_RUN_TEST(test_start_wash_wrong_mode_after_stop_all, "", "验证全停后启动洗车因模式拒绝");
    WDF_RUN_TEST(test_start_wash_denied_with_blocking_alarm, "", "验证存在阻断报警时拒绝启动洗车");
    WDF_RUN_TEST(test_start_wash_denied_when_vehicle_not_ready, "", "验证启动洗车被拒绝时车辆未就绪");
    WDF_RUN_TEST(test_estop_blocks_recover, "", "验证急停阻止恢复");
    WDF_RUN_TEST(test_estop_release_stays_stopped, "", "验证急停解除保持停止模式");
    WDF_RUN_TEST(test_recover_from_stopped_enters_recovering, "", "验证停止模式恢复进入恢复中");
    WDF_RUN_TEST(test_recover_denied_when_service_disabled_after_critical, "", "验证服务关闭后拒绝恢复");
    WDF_RUN_TEST(test_wash_session_lifecycle, "", "验证洗车会话生命周期");
    WDF_RUN_TEST(test_wash_done_with_blocking_enters_stopped, "", "验证洗车完成时存在阻断报警则进入停止");
    WDF_RUN_TEST(test_wash_done_after_cleared_auto_static_enters_wash_done,
                 "ALRM-21",
                 "验证已清除 AUTO_STATIC 不因 journal 进入 STOPPED");
    WDF_RUN_TEST(test_manual_actuator_allowed_in_stopped, "", "验证手动执行器被允许在停止模式");
    WDF_RUN_TEST(test_manual_actuator_denied_with_estop, "", "验证急停时拒绝手动执行器");
    WDF_RUN_TEST(test_self_check_from_stopped_success_returns_stopped, "", "验证停止模式自检成功后返回停止");
    WDF_RUN_TEST(test_self_check_from_stopped_fail_returns_stopped, "", "验证停止模式自检失败后仍为停止");
    WDF_RUN_TEST(test_self_check_denied_when_estop, "", "验证急停时拒绝自检");
    WDF_RUN_TEST(test_stop_all_from_idle_enters_stopped, "", "验证全停从空闲进入停止");
    WDF_RUN_TEST(test_stop_all_from_washing_skips_abort_homing, "", "验证全停洗车中不清障");
    WDF_RUN_TEST(test_stop_all_from_recovering_enters_stopped, "", "验证全停打断恢复进入停止");
    WDF_RUN_TEST(test_cmd_matrix_exhaustive_64, "", "验证命令许可矩阵穷举");

    return UNITY_END();
}
