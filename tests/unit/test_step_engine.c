/**
 * @file    test_step_engine.c
 * @brief   step_engine 单元测试（使用模拟 HAL）
 * @author  HUWANGWEI
 * @date    2026-04-10
 *
 * @note    测试策略：
 *          - 注册模拟 HAL 操作表（不依赖真实硬件）
 *          - 模拟层内部维护虚拟状态（限位 / VFD 运行等）
 *          - 通过时间推进（模拟 poll 间隔）验证步骤完成和超时
 */

#include "application/orchestrators/wash_orchestrator.h"
#include "domain/process/recipe.h"
#include "domain/model/wash_types.h"
#include "domain/device/actuator/motor/motor.h"
#include "domain/device/unit/brush.h"
#include "domain/device/unit/gantry.h"
#include "config/machine/m8_motor_table.h"
#include "ports/hal/hal_motor_port.h"
#include "ports/hal/hal_sensor_port.h"
#include "adapters/machine/m8/m8_sensor.h"
#include "ports/hal/hal_vfd_port.h"
#include "domain/device/water.h"
#include "domain/device/water_channel.h"
#include "core/event_bus/event_bus.h"
#include "common/sw_error.h"
#include "unity.h"

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

static bool mock_sensor_is_active(hal_sensor_channel_t ch)
{
    switch (ch)
    {
        case M8_SIG_ESTOP:          return s_mock_estop;
        case M8_SIG_GANTRY_FWD_LIM: return s_mock_fwd_limit;
        case M8_SIG_GANTRY_REV_LIM: return s_mock_rev_limit;
        case M8_SIG_LIFT_UP_LIM:    return s_mock_lift_top;
        case M8_SIG_LIFT_DOWN_LIM:  return s_mock_lift_bottom;
        default:                    return false;
    }
}

static sw_err_t mock_sensor_init(void) { return SW_OK; }
static void     mock_sensor_tick(void) {}

static const hal_sensor_ops_t s_mock_sensor_ops = {
    .init      = mock_sensor_init,
    .tick      = mock_sensor_tick,
    .is_active = mock_sensor_is_active,
};

static sw_err_t mock_vfd_get_fault_code(hal_vfd_id_t id, uint16_t *p_code)
{
    (void)id;
    if (p_code != NULL) { *p_code = 0U; }
    return SW_OK;
}

static const hal_vfd_ops_t s_mock_vfd_ops = {
    .get_fault_code = mock_vfd_get_fault_code,
};

static sw_err_t mock_motor_set_output(int id, int speed_ref)         { (void)id; (void)speed_ref; return SW_OK; }
static bool     mock_motor_at_fwd_limit(int id)                      { return (id == MOTOR_GANTRY) ? s_mock_fwd_limit : false; }
static bool     mock_motor_at_rev_limit(int id)                      { return (id == MOTOR_GANTRY) ? s_mock_rev_limit : false; }
static bool     mock_motor_encoder_counter_online(int id)            { return id == MOTOR_GANTRY; }
static sw_err_t mock_motor_read_hw_pulse(int id, uint32_t *p_value)  { (void)id; (void)p_value; return SW_ERR_PARAM; }
static sw_err_t mock_motor_clear_hw_pulse(int id)                    { (void)id; return SW_ERR_PARAM; }
static sw_err_t mock_motor_read_current(int id, uint16_t *p_current) { (void)id; (void)p_current; return SW_ERR_PARAM; }
static sw_err_t mock_motor_read_status(int id, uint16_t *p_status)   { (void)id; (void)p_status; return SW_ERR_PARAM; }

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

static sw_err_t mock_water_slot_set(water_channel_t ch, water_slot_t slot, bool on)
{
    (void)ch; (void)slot; (void)on;
    return SW_OK;
}

static void reset_mock_state(void)
{
    s_mock_fwd_limit   = false;
    s_mock_rev_limit   = false;
    s_mock_lift_top    = false;
    s_mock_lift_bottom = false;
    s_mock_estop       = false;
    s_mock_gantry_pos  = 0;
}

void setUp(void)
{
    reset_mock_state();
    wash_exec_clear_abort();
}

void tearDown(void) {}

/* -------------------------------------------------------------------------
 * 测试用例
 * ------------------------------------------------------------------------- */

/* TC-1：步骤正常完成（前限位触发退出）*/
static void test_step_normal_fwd_limit(void)
{
    wash_step_config_t step = {
        .step             = WASH_STEP_PREWASH,
        .name             = "预洗",
        .gantry_freq      = 2500U,
        .gantry_fwd       = true,
        .water_prewash    = true,
        .exit_at_fwd_limit = true,
        .exit_pos_pulse   = -1,
    };

    s_mock_fwd_limit = true;
    sw_err_t ret = wash_exec_step(&step, 1000U, 4500U);
    TEST_ASSERT_EQUAL_INT(SW_OK, ret);
}

/* TC-2：步骤超时 */
static void test_step_timeout(void)
{
    wash_step_config_t step = {
        .step             = WASH_STEP_PREWASH,
        .name             = "预洗超时",
        .gantry_freq      = 2500U,
        .gantry_fwd       = true,
        .exit_at_fwd_limit = true,
        .exit_pos_pulse   = -1,
    };

    sw_err_t ret = wash_exec_step(&step, 60U, 4500U);
    TEST_ASSERT_EQUAL_INT(SW_ERR_TIMEOUT, ret);
}

/* TC-3：wash_orchestrator_abort() 中止当前步骤 */
static void test_step_abort_flag(void)
{
    wash_orchestrator_abort();

    wash_step_config_t step = {
        .step             = WASH_STEP_PREWASH,
        .name             = "预洗(中止)",
        .gantry_freq      = 2500U,
        .gantry_fwd       = true,
        .exit_at_fwd_limit = true,
        .exit_pos_pulse   = -1,
    };

    sw_err_t ret = wash_exec_step(&step, 5000U, 4500U);
    TEST_ASSERT_EQUAL_INT(SW_ERR_STATE, ret);

    wash_exec_clear_abort();
    s_mock_fwd_limit = true;
    ret = wash_exec_step(&step, 5000U, 4500U);
    TEST_ASSERT_EQUAL_INT(SW_OK, ret);
}

/* TC-4：ENTRY / COMPLETE 步骤无需等待（立即返回 SW_OK）*/
static void test_entry_complete_immediate(void)
{
    wash_step_config_t entry    = { .step = WASH_STEP_ENTRY,    .name = "入场" };
    wash_step_config_t complete = { .step = WASH_STEP_COMPLETE, .name = "完成" };

    TEST_ASSERT_EQUAL_INT(SW_OK, wash_exec_step(&entry,    1000U, 0U));
    TEST_ASSERT_EQUAL_INT(SW_OK, wash_exec_step(&complete, 1000U, 0U));
}

int main(void)
{
    /* 一次性 HAL 注册 + 子系统初始化（幂等，setUp 只做状态重置）*/
    hal_motor_register(&s_mock_motor_ops);
    hal_sensor_register(&s_mock_sensor_ops);
    hal_vfd_register(&s_mock_vfd_ops);
    (void)event_bus_init();
    (void)motor_init();
    (void)brush_init();
    (void)gantry_init();
    (void)water_init(
        &(water_cfg_t){ .valve_open_delay_ms = 0U, .pump_stop_delay_ms = 0U },
        &(water_actuator_ops_t){ .slot_set = mock_water_slot_set });

    UNITY_BEGIN();
    RUN_TEST(test_step_normal_fwd_limit);
    RUN_TEST(test_step_timeout);
    RUN_TEST(test_step_abort_flag);
    RUN_TEST(test_entry_complete_immediate);
    return UNITY_END();
}
