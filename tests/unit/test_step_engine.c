/**
 * @file    test_step_engine.c
 * @brief   step_engine 单元测试（使用模拟 HAL）
 * @author  胡望伟
 * @date    2026-04-10
 *
 * @note    测试策略：
 *          - 注册模拟 HAL 操作表（不依赖真实硬件）
 *          - 模拟层内部维护虚拟状态（限位 / VFD 运行等）
 *          - 通过时间推进（模拟 poll 间隔）验证步骤完成和超时
 */

#include "domain/process/step_engine.h"
#include "domain/process/recipe.h"
#include "domain/safety/alarm_core.h"
#include "domain/model/alarm_code.h"
#include "domain/model/wash_types.h"
#include "domain/device/actuator/motor/motor.h"
#include "domain/device/unit/brush.h"
#include "domain/device/unit/gantry.h"
#include "config/machine/m8_motor_table.h"
#include "ports/hal/hal_motion_port.h"
#include "ports/hal/hal_motor_port.h"
#include "ports/hal/hal_sensor_port.h"
#include "ports/hal/hal_water_port.h"
#include "core/event_bus/event_bus.h"
#include "common/sw_error.h"
#include <assert.h>
#include <stdio.h>
#include <stdbool.h>
#include <stdint.h>

/* -------------------------------------------------------------------------
 * 模拟状态
 * ------------------------------------------------------------------------- */
static bool s_mock_fwd_limit   = false;
static bool s_mock_rev_limit   = false;
static bool s_mock_lift_top    = false;
static bool s_mock_lift_bottom = false;
static bool s_mock_estop       = false;
static int  s_mock_gantry_pos  = 0;

/* 模拟 HAL 传感器 */
static bool mock_gantry_at_fwd_limit(void) { return s_mock_fwd_limit; }
static bool mock_gantry_at_rev_limit(void) { return s_mock_rev_limit; }
static bool mock_lift_at_top(void)         { return s_mock_lift_top; }
static bool mock_lift_at_bottom(void)      { return s_mock_lift_bottom; }
static bool mock_is_estop_active(void)     { return s_mock_estop; }
static void mock_poll_input_events(void)   {}
static sw_err_t mock_get_vfd_fault_code(hal_vfd_id_t id, uint16_t *p_code)
{
    (void)id;
    if (p_code != NULL) { *p_code = 0U; }
    return SW_OK;
}

static const hal_sensor_ops_t s_mock_sensor_ops = {
    .gantry_at_fwd_limit  = mock_gantry_at_fwd_limit,
    .gantry_at_rev_limit  = mock_gantry_at_rev_limit,
    .lift_at_top          = mock_lift_at_top,
    .lift_at_bottom       = mock_lift_at_bottom,
    .is_estop_active      = mock_is_estop_active,
    .poll_input_events    = mock_poll_input_events,
    .get_vfd_fault_code   = mock_get_vfd_fault_code,
};

static sw_err_t mock_motor_set_output(int id, int speed_ref)
{
    (void)id;
    (void)speed_ref;
    return SW_OK;
}

static bool mock_motor_at_fwd_limit(int id)
{
    return (id == MOTOR_GANTRY) ? s_mock_fwd_limit : false;
}

static bool mock_motor_at_rev_limit(int id)
{
    return (id == MOTOR_GANTRY) ? s_mock_rev_limit : false;
}

static bool mock_motor_encoder_counter_online(int id)
{
    return id == MOTOR_GANTRY;
}

static sw_err_t mock_motor_read_hw_pulse(int id, uint32_t *p_value)
{
    (void)id;
    (void)p_value;
    return SW_ERR_PARAM;
}

static sw_err_t mock_motor_clear_hw_pulse(int id)
{
    (void)id;
    return SW_ERR_PARAM;
}

static sw_err_t mock_motor_read_current(int id, uint16_t *p_current)
{
    (void)id;
    (void)p_current;
    return SW_ERR_PARAM;
}

static sw_err_t mock_motor_read_status(int id, uint16_t *p_status)
{
    (void)id;
    (void)p_status;
    return SW_ERR_PARAM;
}

static const hal_motor_ops_t s_mock_motor_ops = {
    .set_output             = mock_motor_set_output,
    .at_fwd_limit           = mock_motor_at_fwd_limit,
    .at_rev_limit           = mock_motor_at_rev_limit,
    .encoder_counter_online = mock_motor_encoder_counter_online,
    .read_hw_pulse          = mock_motor_read_hw_pulse,
    .clear_hw_pulse         = mock_motor_clear_hw_pulse,
    .read_current           = mock_motor_read_current,
    .read_status            = mock_motor_read_status,
};

/* 模拟 HAL 运动控制 */
static sw_err_t mock_gantry_fwd(uint16_t f)     { (void)f; return SW_OK; }
static sw_err_t mock_gantry_rev(uint16_t f)     { (void)f; return SW_OK; }
static sw_err_t mock_gantry_stop(void)           { return SW_OK; }
static sw_err_t mock_gantry_fault_reset(void)    { return SW_OK; }
static sw_err_t mock_brush_select(hal_brush_sel_t s) { (void)s; return SW_OK; }
static sw_err_t mock_brush_run(uint16_t f)       { (void)f; return SW_OK; }
static sw_err_t mock_brush_stop(void)            { return SW_OK; }
static sw_err_t mock_brush_fault_reset(void)     { return SW_OK; }
static sw_err_t mock_lift_up_start(uint32_t p)
{
    (void)p;
    s_mock_lift_top    = true;
    s_mock_lift_bottom = false;
    return SW_OK;
}
static sw_err_t mock_lift_down_start(uint32_t p)
{
    (void)p;
    s_mock_lift_bottom = true;
    s_mock_lift_top    = false;
    return SW_OK;
}
/* 说明：lift_at_top/bottom 属于传感器接口，不在 hal_motion_ops_t 中 */

static const hal_motion_ops_t s_mock_motion_ops = {
    .gantry_fwd         = mock_gantry_fwd,
    .gantry_rev         = mock_gantry_rev,
    .gantry_stop        = mock_gantry_stop,
    .gantry_fault_reset = mock_gantry_fault_reset,
    .brush_select       = mock_brush_select,
    .brush_run          = mock_brush_run,
    .brush_stop         = mock_brush_stop,
    .brush_fault_reset  = mock_brush_fault_reset,
    .lift_up_start      = mock_lift_up_start,
    .lift_down_start    = mock_lift_down_start,
};

/* 模拟 HAL 水路控制 */
static sw_err_t mock_pump_set(bool on)         { (void)on; return SW_OK; }
static sw_err_t mock_curtain_set(bool on)      { (void)on; return SW_OK; }
static sw_err_t mock_foam_set(bool on)         { (void)on; return SW_OK; }
static sw_err_t mock_brush_water_set(bool on)  { (void)on; return SW_OK; }
static sw_err_t mock_highpres_set(bool on)     { (void)on; return SW_OK; }
static sw_err_t mock_water_all_off(void)        { return SW_OK; }

static const hal_water_ops_t s_mock_water_ops = {
    .pump_set        = mock_pump_set,
    .curtain_set     = mock_curtain_set,
    .foam_set        = mock_foam_set,
    .brush_water_set = mock_brush_water_set,
    .highpres_set    = mock_highpres_set,
    .all_off         = mock_water_all_off,
};

static void reset_mock_state(void)
{
    s_mock_fwd_limit   = false;
    s_mock_rev_limit   = false;
    s_mock_lift_top    = false;
    s_mock_lift_bottom = false;
    s_mock_estop       = false;
    s_mock_gantry_pos  = 0;
}

/* -------------------------------------------------------------------------
 * 测试用例
 * ------------------------------------------------------------------------- */

/* TC-1：步骤正常完成（前限位触发退出）*/
static void test_step_normal_fwd_limit(void)
{
    printf("TC-1: step normal completion (fwd limit)\n");
    reset_mock_state();
    (void)alarm_core_init();
    step_engine_clear_abort();

    /* 构造一个 PREWASH 步骤：龙门前进，退出条件=前限位 */
    wash_step_config_t step = {
        .step             = WASH_STEP_PREWASH,
        .name             = "预洗",
        .gantry_freq      = 2500U,
        .gantry_fwd       = true,
        .water_prewash    = true,
        .exit_at_fwd_limit = true,
        .exit_pos_pulse   = -1,
    };

    /* 在另一个线程中延迟触发前限位... 此处简化：
     * step_engine_exec_step 的 wait_exit 每 50ms poll 一次，
     * 通过超时短+提前触发限位来测试正常退出路径。
     *
     * 实际做法：设置极短超时+预设限位=true（立即退出）*/
    s_mock_fwd_limit = true; /* 预设：进入 wait_exit 第一次 poll 就满足 */

    sw_err_t ret = step_engine_exec_step(&step, 1000U, 4500U);
    assert(ret == SW_OK);

    printf("  PASS\n");
}

/* TC-2：步骤超时 */
static void test_step_timeout(void)
{
    printf("TC-2: step timeout\n");
    reset_mock_state();
    (void)alarm_core_init();
    step_engine_clear_abort();

    /* 设置极短超时（60ms < poll 间隔 50ms × 2 = 100ms），限位永远不触发 */
    wash_step_config_t step = {
        .step             = WASH_STEP_PREWASH,
        .name             = "预洗超时",
        .gantry_freq      = 2500U,
        .gantry_fwd       = true,
        .exit_at_fwd_limit = true,
        .exit_pos_pulse   = -1,
    };

    sw_err_t ret = step_engine_exec_step(&step, 60U /* ms */, 4500U);
    assert(ret == SW_ERR_TIMEOUT);

    printf("  PASS\n");
}

/* TC-3：步骤中急停触发 → SW_ERR_STATE */
static void test_step_abort_on_alarm(void)
{
    printf("TC-3: abort on ERROR alarm\n");
    reset_mock_state();
    (void)alarm_core_init();
    step_engine_clear_abort();

    /* 预设：第一次 poll 时急停已激活 */
    alarm_core_set_state(ALARM_CODE_ESTOP, true, false);

    wash_step_config_t step = {
        .step             = WASH_STEP_PREWASH,
        .name             = "预洗(带报警)",
        .gantry_freq      = 2500U,
        .gantry_fwd       = true,
        .exit_at_fwd_limit = true,
        .exit_pos_pulse   = -1,
    };

    sw_err_t ret = step_engine_exec_step(&step, 5000U, 4500U);
    assert(ret == SW_ERR_STATE);

    printf("  PASS\n");
}

/* TC-4：step_engine_abort() 中止当前步骤 */
static void test_step_abort_flag(void)
{
    printf("TC-4: step_engine_abort() flag\n");
    reset_mock_state();
    (void)alarm_core_init();

    /* 预设中止标志 */
    step_engine_abort();

    wash_step_config_t step = {
        .step             = WASH_STEP_PREWASH,
        .name             = "预洗(中止)",
        .gantry_freq      = 2500U,
        .gantry_fwd       = true,
        .exit_at_fwd_limit = true,
        .exit_pos_pulse   = -1,
    };

    sw_err_t ret = step_engine_exec_step(&step, 5000U, 4500U);
    assert(ret == SW_ERR_STATE);

    /* 清除后正常执行 */
    step_engine_clear_abort();
    s_mock_fwd_limit = true;
    ret = step_engine_exec_step(&step, 5000U, 4500U);
    assert(ret == SW_OK);

    printf("  PASS\n");
}

/* TC-5：ENTRY / COMPLETE 步骤无需等待（立即返回 SW_OK）*/
static void test_entry_complete_immediate(void)
{
    printf("TC-5: ENTRY/COMPLETE immediate return\n");
    reset_mock_state();
    (void)alarm_core_init();
    step_engine_clear_abort();

    wash_step_config_t entry = {
        .step = WASH_STEP_ENTRY,
        .name = "入场",
    };
    wash_step_config_t complete = {
        .step = WASH_STEP_COMPLETE,
        .name = "完成",
    };

    assert(step_engine_exec_step(&entry,    1000U, 0U) == SW_OK);
    assert(step_engine_exec_step(&complete, 1000U, 0U) == SW_OK);

    printf("  PASS\n");
}

/* -------------------------------------------------------------------------
 * 主函数
 * ------------------------------------------------------------------------- */
int main(void)
{
    printf("=== test_step_engine ===\n");

    /* 注册模拟 HAL */
    hal_motion_register(&s_mock_motion_ops);
    hal_motor_register(&s_mock_motor_ops);
    hal_sensor_register(&s_mock_sensor_ops);
    hal_water_register(&s_mock_water_ops);

    /* 初始化基础组件 */
    (void)event_bus_init();
    (void)alarm_core_init();
    (void)motor_init();
    (void)brush_init();
    (void)gantry_init();
    (void)step_engine_init();

    test_step_normal_fwd_limit();
    test_step_timeout();
    test_step_abort_on_alarm();
    test_step_abort_flag();
    test_entry_complete_immediate();

    printf("=== ALL PASSED ===\n");
    return 0;
}
