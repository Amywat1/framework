/**
 * @file    test_safety_fsm.c
 * @brief   safety_fsm 单元测试
 * @author  胡望伟
 * @date    2026-04-10
 *
 * @note    本测试直接驱动 alarm_core，并显式调用 safety_fsm_reevaluate()，
 *          避免启动线程和异步分发带来的不确定性。
 */

#include "domain/safety/safety_fsm.h"
#include "service/dev_ctx/dev_ctx.h"
#include "domain/safety/alarm_core.h"
#include "domain/model/alarm_code.h"
#include "domain/model/safety_types.h"
#include "core/event_bus/event_bus.h"
#include <assert.h>
#include <stdio.h>

/* -------------------------------------------------------------------------
 * 测试辅助：模拟报警激活后手动触发 safety_fsm 重评估
 * 单测中不启动异步分发线程，直接调用重评估入口。
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
    /* Modbus 通信报警有恢复防抖，tick 足够时长后再重评估 */
    alarm_core_tick_ms(2500);
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

/* TC-3：Modbus 通信丢失 → WARNING（WARNING 级）*/
static void test_modbus_to_warning(void)
{
    printf("TC-3: Modbus lost → WARNING\n");
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
    (void)dev_ctx_init();

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
