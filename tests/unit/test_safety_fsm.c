/**
 * @file    test_safety_fsm.c
 * @brief   safety_fsm 单元测试
 * @author  胡望伟
 * @date    2026-04-10
 *
 * @note    测试策略（简化版，非完整集成验证）：
 *          1. 直接操作 alarm_core 置位/清除报警（不经 event_bus 异步路径）
 *          2. 手动调用 safety_fsm_reevaluate() 模拟事件 handler 被调用的效果
 *          3. 验证 safety_fsm_get_state() 状态转移正确
 *
 *          简化说明：
 *          真实路径是 alarm_core → event_publish → event_dispatch_thread →
 *          on_alarm_triggered → do_reevaluate。
 *          此处绕过 event_bus 异步分发，直接调用 reevaluate，
 *          避免单测中启动线程带来的非确定性。
 *          完整事件驱动路径由 tests/scenario/ 级集成测试覆盖。
 */

#include "domain/safety/safety_fsm.h"
#include "domain/safety/alarm_core.h"
#include "domain/model/alarm_code.h"
#include "domain/model/safety_types.h"
#include "core/event_bus/event_bus.h"
#include <assert.h>
#include <stdio.h>

/* -------------------------------------------------------------------------
 * 测试辅助：模拟报警激活后手动触发 safety_fsm 重评估
 * （替代 event_dispatch_thread 的作用，单测中不启动线程）
 * ------------------------------------------------------------------------- */
static void activate_alarm_and_eval(uint16_t code, bool just_notice)
{
    alarm_core_set_state(code, true, just_notice);
    safety_fsm_reevaluate(); /* 手动触发，模拟 event handler 被调用 */
}

static void clear_alarm_and_eval(uint16_t code)
{
    /* 对于 AUTO 恢复报警，直接 set_state(false) + 足够 tick 清除 */
    alarm_core_set_state(code, false, false);
    /* 对于 0 recover_ms，tick 一次即可清除 */
    alarm_core_tick_ms(10);
    safety_fsm_reevaluate();
}

/* -------------------------------------------------------------------------
 * 测试用例
 * ------------------------------------------------------------------------- */

/* TC-1：急停 → LOCKOUT */
static void test_estop_to_lockout(void)
{
    printf("TC-1: ESTOP → LOCKOUT\n");
    (void)alarm_core_init();
    (void)safety_fsm_init();

    assert(safety_fsm_get_state() == SAFETY_STATE_OK);

    activate_alarm_and_eval(ALARM_CODE_ESTOP, false);
    assert(safety_fsm_get_state() == SAFETY_STATE_LOCKOUT);

    printf("  PASS\n");
}

/* TC-2：VFD 故障 → LOCKOUT（ERROR 级）*/
static void test_vfd_fault_to_lockout(void)
{
    printf("TC-2: VFD fault → LOCKOUT\n");
    (void)alarm_core_init();
    (void)safety_fsm_init();

    activate_alarm_and_eval(ALARM_CODE_VFD_GANTRY, false);
    assert(safety_fsm_get_state() == SAFETY_STATE_LOCKOUT);

    printf("  PASS\n");
}

/* TC-3：Modbus 通信超时 → WARNING（WARNING 级）*/
static void test_modbus_to_warning(void)
{
    printf("TC-3: Modbus timeout → WARNING\n");
    (void)alarm_core_init();
    (void)safety_fsm_init();

    activate_alarm_and_eval(ALARM_CODE_MODBUS_GANTRY, false);
    assert(safety_fsm_get_state() == SAFETY_STATE_WARNING);

    printf("  PASS\n");
}

/* TC-4：WARNING 状态下再触发 ERROR → LOCKOUT */
static void test_warning_then_error_to_lockout(void)
{
    printf("TC-4: WARNING + ERROR → LOCKOUT\n");
    (void)alarm_core_init();
    (void)safety_fsm_init();

    activate_alarm_and_eval(ALARM_CODE_MODBUS_GANTRY, false);
    assert(safety_fsm_get_state() == SAFETY_STATE_WARNING);

    activate_alarm_and_eval(ALARM_CODE_ESTOP, false);
    assert(safety_fsm_get_state() == SAFETY_STATE_LOCKOUT);

    printf("  PASS\n");
}

/* TC-5：WARNING 报警清除 → OK */
static void test_warning_cleared_to_ok(void)
{
    printf("TC-5: WARNING cleared → OK\n");
    (void)alarm_core_init();
    (void)safety_fsm_init();

    activate_alarm_and_eval(ALARM_CODE_MODBUS_GANTRY, false);
    assert(safety_fsm_get_state() == SAFETY_STATE_WARNING);

    clear_alarm_and_eval(ALARM_CODE_MODBUS_GANTRY);
    assert(safety_fsm_get_state() == SAFETY_STATE_OK);

    printf("  PASS\n");
}

/* TC-6：LOCKOUT 手动复位（急停松开 + manual_reset）→ OK */
static void test_manual_reset_to_ok(void)
{
    printf("TC-6: LOCKOUT → manual_reset → OK\n");
    (void)alarm_core_init();
    (void)safety_fsm_init();

    /* 急停激活 */
    alarm_core_set_raw_trigger(ALARM_CODE_ESTOP, true, false);
    alarm_core_tick_ms(10);
    safety_fsm_reevaluate();
    assert(safety_fsm_get_state() == SAFETY_STATE_LOCKOUT);

    /* 急停松开 */
    alarm_core_set_raw_trigger(ALARM_CODE_ESTOP, false, false);

    /* 手动复位 */
    alarm_core_manual_reset();
    safety_fsm_reevaluate(); /* 复位后重新评估 */
    assert(safety_fsm_get_state() == SAFETY_STATE_OK);

    printf("  PASS\n");
}

/* TC-7：NOTICE 级报警不影响 safety_fsm 状态 */
static void test_notice_no_state_change(void)
{
    printf("TC-7: NOTICE alarm doesn't change safety state\n");
    (void)alarm_core_init();
    (void)safety_fsm_init();

    activate_alarm_and_eval(ALARM_CODE_MQTT_OFFLINE, false);
    assert(safety_fsm_get_state() == SAFETY_STATE_OK);

    printf("  PASS\n");
}

/* -------------------------------------------------------------------------
 * 主函数
 * ------------------------------------------------------------------------- */
int main(void)
{
    printf("=== test_safety_fsm ===\n");

    (void)event_bus_init();

    test_estop_to_lockout();
    test_vfd_fault_to_lockout();
    test_modbus_to_warning();
    test_warning_then_error_to_lockout();
    test_warning_cleared_to_ok();
    test_manual_reset_to_ok();
    test_notice_no_state_change();

    printf("=== ALL PASSED ===\n");
    return 0;
}
