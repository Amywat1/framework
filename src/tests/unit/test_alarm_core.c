/**
 * @file    test_alarm_core.c
 * @brief   alarm_core 单元测试
 * @author  胡望伟
 * @date    2026-04-10
 *
 * @note    测试策略：
 *          - 直接调用 alarm_core API，不依赖真实硬件
 *          - event_bus 需先初始化（事件发布走真实总线，但无订阅者也不出错）
 *          - 使用 assert() 断言，失败则 abort
 */

#include "domain/safety/alarm_core.h"
#include "domain/model/alarm_code.h"
#include "core/event_bus/event_bus.h"
#include "common/event_types.h"
#include <assert.h>
#include <stdio.h>
#include <stdbool.h>
#include <stdint.h>

/* -------------------------------------------------------------------------
 * 测试辅助
 * ------------------------------------------------------------------------- */
static int s_triggered_count = 0;
static int s_cleared_count   = 0;

static void on_alarm_triggered(const event_t *evt)
{
    s_triggered_count++;
    printf("  [EVT] TRIGGERED code=%u\n", (unsigned)evt->param);
}

static void on_alarm_cleared(const event_t *evt)
{
    s_cleared_count++;
    printf("  [EVT] CLEARED code=%u\n", (unsigned)evt->param);
}

static void reset_counters(void)
{
    s_triggered_count = 0;
    s_cleared_count   = 0;
}

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
    reset_counters();

    /* ALARM_CODE_GANTRY_FWD_LIM trigger_ms = 500ms → 需 50 个 10ms tick */
    alarm_core_set_raw_trigger(ALARM_CODE_GANTRY_FWD_LIM, true, false);

    /* 49 个 tick（490ms）：尚未激活 */
    run_ticks(49);
    assert(!alarm_core_is_active(ALARM_CODE_GANTRY_FWD_LIM));

    /* 第 50 个 tick（500ms）：应激活 */
    alarm_core_tick_ms(ALARM_POLL_PERIOD_MS);
    assert(alarm_core_is_active(ALARM_CODE_GANTRY_FWD_LIM));
    assert(alarm_core_has_error());
    assert(s_triggered_count == 1);

    printf("  PASS\n");
}

/* TC-2：AUTO 恢复（触发消失后经过 recover_ms 自动清除）*/
static void test_auto_recovery(void)
{
    printf("TC-2: AUTO recovery\n");
    (void)alarm_core_init();
    reset_counters();

    /* 先激活 */
    alarm_core_set_raw_trigger(ALARM_CODE_GANTRY_FWD_LIM, true, false);
    run_ticks(50);  /* 触发 */
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
    assert(s_cleared_count == 1);

    printf("  PASS\n");
}

/* TC-3：MANUAL 复位（急停按下后只能手动复位，触发消失后仍不自动清除）*/
static void test_manual_reset(void)
{
    printf("TC-3: MANUAL reset (ESTOP)\n");
    (void)alarm_core_init();
    reset_counters();

    /* 急停 trigger_ms=0，set_raw_trigger 后 tick 立即激活 */
    alarm_core_set_raw_trigger(ALARM_CODE_ESTOP, true, false);
    alarm_core_tick_ms(ALARM_POLL_PERIOD_MS);
    assert(alarm_core_is_active(ALARM_CODE_ESTOP));

    /* 急停按钮松开（raw_trigger=false），tick 100次也不应自动清除 */
    alarm_core_set_raw_trigger(ALARM_CODE_ESTOP, false, false);
    run_ticks(100);
    assert(alarm_core_is_active(ALARM_CODE_ESTOP));

    /* 手动复位：此时 raw_triggered=false，应成功清除 */
    alarm_core_manual_reset();
    assert(!alarm_core_is_active(ALARM_CODE_ESTOP));
    assert(s_cleared_count == 1);

    printf("  PASS\n");
}

/* TC-4：manual_reset 时触发条件仍在，不允许清除 */
static void test_manual_reset_blocked_if_trigger_active(void)
{
    printf("TC-4: manual_reset blocked when trigger still active\n");
    (void)alarm_core_init();
    reset_counters();

    /* 急停激活（按钮仍按着）*/
    alarm_core_set_raw_trigger(ALARM_CODE_ESTOP, true, false);
    alarm_core_tick_ms(ALARM_POLL_PERIOD_MS);
    assert(alarm_core_is_active(ALARM_CODE_ESTOP));

    /* 此时 raw_triggered=true，manual_reset 不应清除 */
    alarm_core_manual_reset();
    assert(alarm_core_is_active(ALARM_CODE_ESTOP));
    assert(s_cleared_count == 0);

    printf("  PASS\n");
}

/* TC-5：set_state 直报（跳过防抖，立即激活）*/
static void test_direct_set_state(void)
{
    printf("TC-5: set_state direct (no debounce)\n");
    (void)alarm_core_init();
    reset_counters();

    /* 龙门 VFD 故障（trigger_ms=200ms）：set_state 直接激活，不等 200ms */
    alarm_core_set_state(ALARM_CODE_VFD_GANTRY, true, false);
    assert(alarm_core_is_active(ALARM_CODE_VFD_GANTRY));
    assert(s_triggered_count == 1);

    printf("  PASS\n");
}

/* TC-6：等级判断（has_error / has_warning 语义正确）*/
static void test_level_classification(void)
{
    printf("TC-6: level classification\n");
    (void)alarm_core_init();

    /* 仅 NOTICE 级报警（云端断线）：not error, not warning */
    alarm_core_set_state(ALARM_CODE_MQTT_OFFLINE, true, false);
    assert(!alarm_core_has_error());
    assert(!alarm_core_has_warning());
    assert(alarm_core_is_active(ALARM_CODE_MQTT_OFFLINE));

    /* 再加 WARNING 级 */
    alarm_core_set_state(ALARM_CODE_MODBUS_GANTRY, true, false);
    assert(!alarm_core_has_error());
    assert(alarm_core_has_warning());

    /* 再加 ERROR 级 */
    alarm_core_set_state(ALARM_CODE_ESTOP, true, false);
    assert(alarm_core_has_error());

    printf("  PASS\n");
}

/* TC-7：just_notice 降级（硬件未安装时强制 NOTICE）*/
static void test_just_notice_downgrade(void)
{
    printf("TC-7: just_notice downgrade\n");
    (void)alarm_core_init();

    /* LIFT_UP_LIM 本来是 ERROR，降级为 NOTICE */
    alarm_core_set_raw_trigger(ALARM_CODE_LIFT_UP_LIM, true, true /* just_notice */);
    run_ticks(50);  /* 触发防抖 */
    assert(alarm_core_is_active(ALARM_CODE_LIFT_UP_LIM));
    /* 降级后不应触发 has_error */
    assert(!alarm_core_has_error());
    assert(!alarm_core_has_warning());

    printf("  PASS\n");
}

/* -------------------------------------------------------------------------
 * 主函数
 * ------------------------------------------------------------------------- */
int main(void)
{
    printf("=== test_alarm_core ===\n");

    /* event_bus 需先初始化（alarm_core 内部会发布事件）*/
    (void)event_bus_init();
    (void)event_subscribe(EVT_ALARM_TRIGGERED, on_alarm_triggered);
    (void)event_subscribe(EVT_ALARM_CLEARED,   on_alarm_cleared);

    test_debounce_trigger();
    test_auto_recovery();
    test_manual_reset();
    test_manual_reset_blocked_if_trigger_active();
    test_direct_set_state();
    test_level_classification();
    test_just_notice_downgrade();

    printf("=== ALL PASSED ===\n");
    return 0;
}
