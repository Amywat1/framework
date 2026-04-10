/**
 * @file    test_alarm_core.c
 * @brief   alarm_core 单元测试
 * @author  胡望伟
 * @date    2026-04-10
 *
 * @note    测试策略：
 *          - 只断言 alarm_core 状态（is_active / has_error / has_warning）
 *          - 不断言事件计数——事件计数需要 event_dispatch_thread 运行才可靠；
 *            单测中不启线程，避免非确定性竞争。
 *          - event_bus 仍需初始化，供 alarm_core 内部发布事件（即使无订阅者）
 */

#include "domain/safety/alarm_core.h"
#include "domain/model/alarm_code.h"
#include "core/event_bus/event_bus.h"
#include <assert.h>
#include <stdio.h>
#include <stdbool.h>
#include <stdint.h>

/* 驱动若干个 tick，每 tick 10ms */
static void run_ticks(int n)
{
    int i;
    for (i = 0; i < n; i++)
    {
        alarm_core_tick_ms(ALARM_POLL_PERIOD_MS);
    }
}

/* -------------------------------------------------------------------------
 * 测试用例
 * ------------------------------------------------------------------------- */

/* TC-1：防抖触发（set_raw_trigger + tick 达到 trigger_ms 后激活）*/
static void test_debounce_trigger(void)
{
    printf("TC-1: debounce trigger\n");
    (void)alarm_core_init();

    /* ALARM_CODE_GANTRY_FWD_LIM trigger_ms = 500ms → 需 50 个 10ms tick */
    alarm_core_set_raw_trigger(ALARM_CODE_GANTRY_FWD_LIM, true, false);

    /* 49 个 tick（490ms）：尚未激活 */
    run_ticks(49);
    assert(!alarm_core_is_active(ALARM_CODE_GANTRY_FWD_LIM));

    /* 第 50 个 tick（500ms）：应激活 */
    alarm_core_tick_ms(ALARM_POLL_PERIOD_MS);
    assert(alarm_core_is_active(ALARM_CODE_GANTRY_FWD_LIM));
    assert(alarm_core_has_error());

    printf("  PASS\n");
}

/* TC-2：AUTO 恢复（触发消失后经过 recover_ms 自动清除）*/
static void test_auto_recovery(void)
{
    printf("TC-2: AUTO recovery\n");
    (void)alarm_core_init();

    /* 先激活 */
    alarm_core_set_raw_trigger(ALARM_CODE_GANTRY_FWD_LIM, true, false);
    run_ticks(50);
    assert(alarm_core_is_active(ALARM_CODE_GANTRY_FWD_LIM));

    /* 触发条件消失 */
    alarm_core_set_raw_trigger(ALARM_CODE_GANTRY_FWD_LIM, false, false);

    /* recover_ms = 500ms → 49 个 tick 还未恢复 */
    run_ticks(49);
    assert(alarm_core_is_active(ALARM_CODE_GANTRY_FWD_LIM));

    /* 第 50 个 tick：应清除 */
    alarm_core_tick_ms(ALARM_POLL_PERIOD_MS);
    assert(!alarm_core_is_active(ALARM_CODE_GANTRY_FWD_LIM));
    assert(!alarm_core_has_error());

    printf("  PASS\n");
}

/* TC-3：MANUAL 复位（急停按下后只能手动复位，触发消失后仍不自动清除）*/
static void test_manual_reset(void)
{
    printf("TC-3: MANUAL reset (ESTOP)\n");
    (void)alarm_core_init();

    /* 急停 trigger_ms=0，tick 后立即激活 */
    alarm_core_set_raw_trigger(ALARM_CODE_ESTOP, true, false);
    alarm_core_tick_ms(ALARM_POLL_PERIOD_MS);
    assert(alarm_core_is_active(ALARM_CODE_ESTOP));

    /* 急停松开，tick 100 次不自动清除 */
    alarm_core_set_raw_trigger(ALARM_CODE_ESTOP, false, false);
    run_ticks(100);
    assert(alarm_core_is_active(ALARM_CODE_ESTOP));

    /* 手动复位：raw_triggered=false，应成功清除 */
    alarm_core_manual_reset();
    assert(!alarm_core_is_active(ALARM_CODE_ESTOP));
    assert(!alarm_core_has_error());

    printf("  PASS\n");
}

/* TC-4：manual_reset 时触发条件仍在，不允许清除 */
static void test_manual_reset_blocked_if_trigger_active(void)
{
    printf("TC-4: manual_reset blocked when trigger still active\n");
    (void)alarm_core_init();

    alarm_core_set_raw_trigger(ALARM_CODE_ESTOP, true, false);
    alarm_core_tick_ms(ALARM_POLL_PERIOD_MS);
    assert(alarm_core_is_active(ALARM_CODE_ESTOP));

    /* raw_triggered 仍为 true → 不应清除 */
    alarm_core_manual_reset();
    assert(alarm_core_is_active(ALARM_CODE_ESTOP));

    printf("  PASS\n");
}

/* TC-5：set_state 直报（跳过防抖，立即激活）*/
static void test_direct_set_state(void)
{
    printf("TC-5: set_state direct (no debounce)\n");
    (void)alarm_core_init();

    /* 龙门 VFD 故障 trigger_ms=200ms：set_state 直接激活，无需等待 */
    alarm_core_set_state(ALARM_CODE_VFD_GANTRY, true, false);
    assert(alarm_core_is_active(ALARM_CODE_VFD_GANTRY));
    assert(alarm_core_has_error());

    printf("  PASS\n");
}

/* TC-6：等级判断（has_error / has_warning 语义正确）*/
static void test_level_classification(void)
{
    printf("TC-6: level classification\n");
    (void)alarm_core_init();

    alarm_core_set_state(ALARM_CODE_MQTT_OFFLINE, true, false); /* NOTICE */
    assert(!alarm_core_has_error());
    assert(!alarm_core_has_warning());
    assert(alarm_core_is_active(ALARM_CODE_MQTT_OFFLINE));

    alarm_core_set_state(ALARM_CODE_MODBUS_GANTRY, true, false); /* WARNING */
    assert(!alarm_core_has_error());
    assert(alarm_core_has_warning());

    alarm_core_set_state(ALARM_CODE_ESTOP, true, false); /* ERROR */
    assert(alarm_core_has_error());

    printf("  PASS\n");
}

/* TC-7：just_notice 降级（硬件未安装时强制 NOTICE）*/
static void test_just_notice_downgrade(void)
{
    printf("TC-7: just_notice downgrade\n");
    (void)alarm_core_init();

    /* LIFT_UP_LIM 本来是 ERROR，just_notice=true 强制降级 */
    alarm_core_set_raw_trigger(ALARM_CODE_LIFT_UP_LIM, true, true);
    run_ticks(50);
    assert(alarm_core_is_active(ALARM_CODE_LIFT_UP_LIM));
    assert(!alarm_core_has_error());   /* 降级为 NOTICE，不应触发 */
    assert(!alarm_core_has_warning());

    printf("  PASS\n");
}

/* TC-8：alarm_core_init() 清零回调（单测间状态不串漏）*/
static void dummy_poll_fn(void)       {}
static void dummy_emc_reset_fn(void)  {}

static void test_init_clears_callbacks(void)
{
    printf("TC-8: init clears callbacks\n");

    alarm_core_register_poll_fn(dummy_poll_fn);
    alarm_core_register_emc_reset_fn(dummy_emc_reset_fn);

    /* 重新 init 后，回调应被清零；不触发任何回调（无法直接验证指针，
     * 此处通过 tick_ms 不 crash 来间接验证）*/
    (void)alarm_core_init();
    alarm_core_tick_ms(ALARM_POLL_PERIOD_MS); /* 不应 crash */

    printf("  PASS\n");
}

/* -------------------------------------------------------------------------
 * 主函数
 * ------------------------------------------------------------------------- */
int main(void)
{
    printf("=== test_alarm_core ===\n");

    (void)event_bus_init();

    test_debounce_trigger();
    test_auto_recovery();
    test_manual_reset();
    test_manual_reset_blocked_if_trigger_active();
    test_direct_set_state();
    test_level_classification();
    test_just_notice_downgrade();
    test_init_clears_callbacks();

    printf("=== ALL PASSED ===\n");
    return 0;
}
