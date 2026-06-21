/**
 * @file    scenario_limit_error.c
 * @brief   场景测试：限位异常报警触发与自动恢复
 * @author  HUWANGWEI
 * @date    2026-04-10
 *
 * @note    测试策略：
 *          - 验证 just_notice 降级机制（CFG_*_INSTALLED=0 时限位报警降级为 NOTICE）
 *          - 验证 AUTO 恢复路径（防抖触发后，条件消失 + recover_ms 后自动清除）
 *          - 完整事件驱动路径：raw_trigger + tick → EVT_ALARM_TRIGGERED
 *            → dispatch → safety_fsm → 状态更新
 *
 *          限位报警配置（alarm_core.c 中）：
 *            GANTRY_FWD_LIM / GANTRY_REV_LIM : ERROR 级，AUTO 恢复，trigger_ms=500
 *            LIFT_UP_LIM  / LIFT_DOWN_LIM    : ERROR 级，AUTO 恢复，trigger_ms=500
 *            → 若 just_notice=true，降级为 NOTICE，不影响 safety_fsm 状态
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

static void drain_events(void)
{
    usleep(50U * 1000U);
}

/* 驱动若干个 tick（每 tick = ALARM_POLL_PERIOD_MS = 10ms）*/
static void run_ticks(int n)
{
    int i;
    for (i = 0; i < n; i++)
    {
        alarm_core_tick_ms(ALARM_POLL_PERIOD_MS);
    }
}

/* -------------------------------------------------------------------------
 * TC-1：限位异常（正常情况，ERROR 级）→ safety LOCKOUT
 *       防抖触发路径：raw_trigger=true → 500ms 后激活（50 个 10ms tick）
 * ------------------------------------------------------------------------- */
static void tc1_limit_error_triggers_lockout(void)
{
    printf("TC-1: limit error (no downgrade) → LOCKOUT after debounce\n");

    (void)alarm_core_init();
    (void)safety_fsm_init();

    /* 设置原始触发（just_notice=false：不降级，保持 ERROR 级）*/
    alarm_core_set_raw_trigger(ALARM_CODE_GANTRY_FWD_LIM, true, false);

    /* 防抖未到：49 个 tick（490ms）→ 尚未激活 */
    run_ticks(49);
    drain_events();
    assert(!alarm_core_is_active(ALARM_CODE_GANTRY_FWD_LIM));
    assert(safety_fsm_get_state() == SAFETY_STATE_OK);

    /* 第 50 个 tick（500ms）→ 激活 → EVT_ALARM_TRIGGERED → safety LOCKOUT */
    run_ticks(1);
    drain_events();
    assert(alarm_core_is_active(ALARM_CODE_GANTRY_FWD_LIM));
    assert(safety_fsm_get_state() == SAFETY_STATE_LOCKOUT);

    printf("  PASS\n");
}

/* -------------------------------------------------------------------------
 * TC-2：限位异常（降级 NOTICE，just_notice=true）→ safety 状态不变
 *       适用于硬件未安装（CFG_*_INSTALLED=0）时自动调用
 * ------------------------------------------------------------------------- */
static void tc2_limit_downgraded_to_notice_no_lockout(void)
{
    printf("TC-2: limit alarm just_notice=true → safety stays OK\n");

    (void)alarm_core_init();
    (void)safety_fsm_init();

    alarm_core_set_raw_trigger(ALARM_CODE_LIFT_UP_LIM, true, true); /* just_notice=true */
    run_ticks(50); /* 超过防抖时间 */
    drain_events();

    /* 降级为 NOTICE：报警记录激活，但不是 ERROR 或 WARNING */
    assert(alarm_core_is_active(ALARM_CODE_LIFT_UP_LIM));
    assert(!alarm_core_has_error());
    assert(!alarm_core_has_warning());
    assert(safety_fsm_get_state() == SAFETY_STATE_OK);

    printf("  PASS\n");
}

/* -------------------------------------------------------------------------
 * TC-3：AUTO 恢复（触发条件消失 + recover_ms 后自动清除）
 *       路径：raw_trigger=false → 经 recover_ms tick → 清除
 *              → EVT_ALARM_CLEARED → dispatch → safety_fsm → OK
 * ------------------------------------------------------------------------- */
static void tc3_auto_recovery_after_condition_clears(void)
{
    printf("TC-3: AUTO recovery after limit clears\n");

    (void)alarm_core_init();
    (void)safety_fsm_init();

    /* 触发报警（需要先激活）*/
    alarm_core_set_raw_trigger(ALARM_CODE_GANTRY_FWD_LIM, true, false);
    run_ticks(50);
    drain_events();
    assert(alarm_core_is_active(ALARM_CODE_GANTRY_FWD_LIM));
    assert(safety_fsm_get_state() == SAFETY_STATE_LOCKOUT);

    /* 触发条件消失（龙门回到正常区域，限位不再触发）*/
    alarm_core_set_raw_trigger(ALARM_CODE_GANTRY_FWD_LIM, false, false);

    /* AUTO 恢复防抖：recover_ms=500ms → 49 个 tick 仍激活 */
    run_ticks(49);
    drain_events();
    assert(alarm_core_is_active(ALARM_CODE_GANTRY_FWD_LIM));
    assert(safety_fsm_get_state() == SAFETY_STATE_LOCKOUT);

    /* 第 50 个 tick → 自动清除 → EVT_ALARM_CLEARED → safety OK */
    run_ticks(1);
    drain_events();
    assert(!alarm_core_is_active(ALARM_CODE_GANTRY_FWD_LIM));
    assert(safety_fsm_get_state() == SAFETY_STATE_OK);

    printf("  PASS\n");
}

/* -------------------------------------------------------------------------
 * TC-4：多个限位同时触发 → 逐个恢复后 safety 最终 OK
 * ------------------------------------------------------------------------- */
static void tc4_multiple_limits_sequential_recovery(void)
{
    printf("TC-4: multiple limits recover sequentially\n");

    (void)alarm_core_init();
    (void)safety_fsm_init();

    /* 触发两个限位 */
    alarm_core_set_raw_trigger(ALARM_CODE_GANTRY_FWD_LIM, true, false);
    alarm_core_set_raw_trigger(ALARM_CODE_GANTRY_REV_LIM, true, false);
    run_ticks(50);
    drain_events();
    assert(safety_fsm_get_state() == SAFETY_STATE_LOCKOUT);

    /* 恢复第一个 → 触发条件消失 + AUTO 恢复 */
    alarm_core_set_raw_trigger(ALARM_CODE_GANTRY_FWD_LIM, false, false);
    run_ticks(50);
    drain_events();
    assert(!alarm_core_is_active(ALARM_CODE_GANTRY_FWD_LIM));
    assert(safety_fsm_get_state() == SAFETY_STATE_LOCKOUT); /* 第二个仍激活 */

    /* 恢复第二个 */
    alarm_core_set_raw_trigger(ALARM_CODE_GANTRY_REV_LIM, false, false);
    run_ticks(50);
    drain_events();
    assert(!alarm_core_is_active(ALARM_CODE_GANTRY_REV_LIM));
    assert(safety_fsm_get_state() == SAFETY_STATE_OK);

    printf("  PASS\n");
}

/* -------------------------------------------------------------------------
 * 主函数
 * ------------------------------------------------------------------------- */
int main(void)
{
    printf("=== scenario_limit_error ===\n");

    (void)event_bus_init();
    pthread_create(&s_dispatch_tid, NULL, dispatch_fn, NULL);
    usleep(10U * 1000U);

    tc1_limit_error_triggers_lockout();
    tc2_limit_downgraded_to_notice_no_lockout();
    tc3_auto_recovery_after_condition_clears();
    tc4_multiple_limits_sequential_recovery();

    pthread_cancel(s_dispatch_tid);
    pthread_join(s_dispatch_tid, NULL);

    printf("=== ALL PASSED ===\n");
    return 0;
}
