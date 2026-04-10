/**
 * @file    scenario_vfd_fault.c
 * @brief   场景测试：VFD 故障 → LOCKOUT → 故障清除 → 复位 → OK
 * @author  胡望伟
 * @date    2026-04-10
 *
 * @note    测试策略：
 *          - VFD 故障通过 alarm_core_set_state()（直报，无防抖）触发
 *          - 验证事件驱动链：
 *            set_state(true) → EVT_ALARM_TRIGGERED → dispatch
 *            → safety_fsm → LOCKOUT（ERROR 级）
 *          - VFD 故障使用 ALARM_RECOVER_DRIVE 恢复策略：
 *            set_state(false)（驱动事件清除）→ EVT_ALARM_CLEARED
 *            → safety_fsm → OK（需先无 ERROR 报警）
 *          - Modbus 通信超时（WARNING 级）不触发 LOCKOUT
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

/* -------------------------------------------------------------------------
 * TC-1：龙门 VFD 故障（ERROR 级）→ safety LOCKOUT
 *       直报路径（set_state 跳过防抖，立即激活）
 * ------------------------------------------------------------------------- */
static void tc1_vfd_gantry_to_lockout(void)
{
    printf("TC-1: VFD gantry fault → LOCKOUT\n");

    (void)alarm_core_init();
    (void)safety_fsm_init();

    assert(safety_fsm_get_state() == SAFETY_STATE_OK);
    assert(!alarm_core_has_error());

    /* VFD 故障直报（驱动层检测到后调用 set_state）*/
    alarm_core_set_state(ALARM_CODE_VFD_GANTRY, true, false);
    drain_events();

    assert(safety_fsm_get_state() == SAFETY_STATE_LOCKOUT);
    assert(alarm_core_has_error());
    assert(alarm_core_is_active(ALARM_CODE_VFD_GANTRY));

    printf("  PASS\n");
}

/* -------------------------------------------------------------------------
 * TC-2：VFD 故障清除（驱动事件清除）→ safety 恢复 OK
 *       ALARM_RECOVER_DRIVE：驱动层主动清除（set_state false）即可恢复
 * ------------------------------------------------------------------------- */
static void tc2_vfd_fault_cleared_to_ok(void)
{
    printf("TC-2: VFD fault cleared (DRIVE recover) → OK\n");

    (void)alarm_core_init();
    (void)safety_fsm_init();

    /* 先激活 VFD 故障 */
    alarm_core_set_state(ALARM_CODE_VFD_GANTRY, true, false);
    drain_events();
    assert(safety_fsm_get_state() == SAFETY_STATE_LOCKOUT);

    /* 驱动层清除故障（VFD 复位成功后调用）*/
    alarm_core_set_state(ALARM_CODE_VFD_GANTRY, false, false);
    drain_events();

    assert(safety_fsm_get_state() == SAFETY_STATE_OK);
    assert(!alarm_core_has_error());
    assert(!alarm_core_is_active(ALARM_CODE_VFD_GANTRY));

    printf("  PASS\n");
}

/* -------------------------------------------------------------------------
 * TC-3：刷子 VFD + 龙门 VFD 同时故障 → 全部清除才恢复 OK
 * ------------------------------------------------------------------------- */
static void tc3_dual_vfd_fault_requires_both_cleared(void)
{
    printf("TC-3: dual VFD fault — both must clear for OK\n");

    (void)alarm_core_init();
    (void)safety_fsm_init();

    alarm_core_set_state(ALARM_CODE_VFD_GANTRY, true, false);
    alarm_core_set_state(ALARM_CODE_VFD_BRUSH,  true, false);
    drain_events();
    assert(safety_fsm_get_state() == SAFETY_STATE_LOCKOUT);

    /* 清除一个 → 另一个仍激活 → 仍 LOCKOUT */
    alarm_core_set_state(ALARM_CODE_VFD_GANTRY, false, false);
    drain_events();
    assert(safety_fsm_get_state() == SAFETY_STATE_LOCKOUT);
    assert(alarm_core_has_error()); /* VFD_BRUSH 仍激活 */

    /* 清除第二个 → OK */
    alarm_core_set_state(ALARM_CODE_VFD_BRUSH, false, false);
    drain_events();
    assert(safety_fsm_get_state() == SAFETY_STATE_OK);
    assert(!alarm_core_has_error());

    printf("  PASS\n");
}

/* -------------------------------------------------------------------------
 * TC-4：Modbus 通信超时（WARNING 级）不引起 LOCKOUT
 * ------------------------------------------------------------------------- */
static void tc4_modbus_timeout_warning_only(void)
{
    printf("TC-4: Modbus timeout → WARNING, not LOCKOUT\n");

    (void)alarm_core_init();
    (void)safety_fsm_init();

    alarm_core_set_state(ALARM_CODE_MODBUS_GANTRY, true, false);
    drain_events();

    assert(safety_fsm_get_state() == SAFETY_STATE_WARNING);
    assert(!alarm_core_has_error());
    assert(alarm_core_has_warning());

    /* 通信恢复 */
    alarm_core_set_state(ALARM_CODE_MODBUS_GANTRY, false, false);
    drain_events();
    assert(safety_fsm_get_state() == SAFETY_STATE_OK);

    printf("  PASS\n");
}

/* -------------------------------------------------------------------------
 * 主函数
 * ------------------------------------------------------------------------- */
int main(void)
{
    printf("=== scenario_vfd_fault ===\n");

    (void)event_bus_init();
    pthread_create(&s_dispatch_tid, NULL, dispatch_fn, NULL);
    usleep(10U * 1000U);

    tc1_vfd_gantry_to_lockout();
    tc2_vfd_fault_cleared_to_ok();
    tc3_dual_vfd_fault_requires_both_cleared();
    tc4_modbus_timeout_warning_only();

    pthread_cancel(s_dispatch_tid);
    pthread_join(s_dispatch_tid, NULL);

    printf("=== ALL PASSED ===\n");
    return 0;
}
