/**
 * @file    scenario_estop.c
 * @brief   场景测试：急停触发 → 安全 LOCKOUT → 手动复位 → OK
 * @author  胡望伟
 * @date    2026-04-10
 *
 * @note    测试策略：
 *          - 完整事件驱动路径（不绕过 event_bus，补充 test_safety_fsm.c 的直接调用）
 *          - 急停触发路径：alarm_core_set_raw_trigger + tick
 *            → EVT_ALARM_TRIGGERED（经事件总线异步分发）
 *            → safety_fsm.on_alarm_triggered → do_reevaluate
 *            → safety_state = LOCKOUT
 *          - 复位路径：松开急停 + alarm_core_manual_reset
 *            → EVT_ALARM_CLEARED → safety_fsm → safety_state = OK
 */

#include "core/event_bus/event_bus.h"
#include "domain/safety/alarm_core.h"
#include "domain/safety/safety_fsm.h"
#include "domain/model/alarm_code.h"
#include "domain/model/safety_types.h"
#include <assert.h>
#include <stdio.h>
#include <pthread.h>
#include <unistd.h>

/* -------------------------------------------------------------------------
 * 事件分发线程
 * ------------------------------------------------------------------------- */
static pthread_t s_dispatch_tid;

static void *dispatch_fn(void *arg)
{
    (void)arg;
    event_bus_dispatch_loop();
    return NULL;
}

/* 等待事件分发完成（单次分发链不超过 3 跳，50ms 足够）*/
static void drain_events(void)
{
    usleep(50U * 1000U);
}

/* -------------------------------------------------------------------------
 * TC-1：急停按下 → 安全状态机进入 LOCKOUT
 *       路径：raw_trigger=true + tick → alarm 激活 → EVT_ALARM_TRIGGERED
 *              → dispatch → safety_fsm → LOCKOUT
 * ------------------------------------------------------------------------- */
static void tc1_estop_triggers_lockout(void)
{
    printf("TC-1: ESTOP triggers LOCKOUT (via event dispatch)\n");

    (void)alarm_core_init();
    (void)safety_fsm_init();

    assert(safety_fsm_get_state() == SAFETY_STATE_OK);

    /* 急停 trigger_ms=0：一次 tick 即激活 */
    alarm_core_set_raw_trigger(ALARM_CODE_ESTOP, true, false);
    alarm_core_tick_ms(ALARM_POLL_PERIOD_MS);

    /* 等待事件总线分发 → safety_fsm 更新 */
    drain_events();

    assert(safety_fsm_get_state() == SAFETY_STATE_LOCKOUT);
    assert(alarm_core_has_error());

    printf("  PASS\n");
}

/* -------------------------------------------------------------------------
 * TC-2：急停松开 + 手动复位 → 安全状态恢复 OK
 *       路径：raw_trigger=false + manual_reset → alarm 清除 → EVT_ALARM_CLEARED
 *              → dispatch → safety_fsm → OK
 * ------------------------------------------------------------------------- */
static void tc2_release_and_reset_to_ok(void)
{
    printf("TC-2: ESTOP release + manual_reset → OK (via event dispatch)\n");

    (void)alarm_core_init();
    (void)safety_fsm_init();

    /* 先激活急停 */
    alarm_core_set_raw_trigger(ALARM_CODE_ESTOP, true, false);
    alarm_core_tick_ms(ALARM_POLL_PERIOD_MS);
    drain_events();
    assert(safety_fsm_get_state() == SAFETY_STATE_LOCKOUT);

    /* 松开急停（raw_trigger=false），满足手动复位前提 */
    alarm_core_set_raw_trigger(ALARM_CODE_ESTOP, false, false);

    /* MANUAL 恢复：tick 不自动清除，需手动复位 */
    alarm_core_tick_ms(100U * ALARM_POLL_PERIOD_MS); /* 1000ms 远超 recover_ms */
    drain_events();
    assert(safety_fsm_get_state() == SAFETY_STATE_LOCKOUT); /* 仍 LOCKOUT */

    /* 手动复位 → alarm_core 清除 ESTOP → EVT_ALARM_CLEARED → safety_fsm → OK */
    alarm_core_manual_reset();
    drain_events();
    assert(safety_fsm_get_state() == SAFETY_STATE_OK);
    assert(!alarm_core_has_error());

    printf("  PASS\n");
}

/* -------------------------------------------------------------------------
 * TC-3：急停激活时无法手动复位（raw_trigger 仍为 true）
 * ------------------------------------------------------------------------- */
static void tc3_reset_blocked_while_active(void)
{
    printf("TC-3: manual_reset blocked while ESTOP still active\n");

    (void)alarm_core_init();
    (void)safety_fsm_init();

    alarm_core_set_raw_trigger(ALARM_CODE_ESTOP, true, false);
    alarm_core_tick_ms(ALARM_POLL_PERIOD_MS);
    drain_events();
    assert(safety_fsm_get_state() == SAFETY_STATE_LOCKOUT);

    /* 尝试复位（raw_trigger 仍 true → 不允许清除）*/
    alarm_core_manual_reset();
    drain_events();
    assert(safety_fsm_get_state() == SAFETY_STATE_LOCKOUT);
    assert(alarm_core_is_active(ALARM_CODE_ESTOP));

    printf("  PASS\n");
}

/* -------------------------------------------------------------------------
 * 主函数
 * ------------------------------------------------------------------------- */
int main(void)
{
    printf("=== scenario_estop ===\n");

    /* 初始化事件总线并启动分发线程 */
    (void)event_bus_init();
    pthread_create(&s_dispatch_tid, NULL, dispatch_fn, NULL);
    usleep(10U * 1000U); /* 等待线程启动 */

    tc1_estop_triggers_lockout();
    tc2_release_and_reset_to_ok();
    tc3_reset_blocked_while_active();

    pthread_cancel(s_dispatch_tid);
    pthread_join(s_dispatch_tid, NULL);

    printf("=== ALL PASSED ===\n");
    return 0;
}
