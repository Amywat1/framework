/**
 * @file    test_hal_motor_sim.c
 * @brief   hal_motor_sim 电机仿真 HAL 单元测试
 *
 * 前提：hal_io_sim 注册后提供 DI/脉冲读写后端；
 *       sim_encoder_counter 由 hal_motor_sim_register() 隐式重置。
 *
 * 分组：
 *   A. 绑定参数校验
 *   B. 速度输出（无回调 / 有回调）
 *   C. 限位检测
 *   D. 编码器脉冲计数器
 *   E. 电流 / 状态 / 故障复位回调
 */

#include "framework/adapters/outbound/hal/sim/hal_motor_sim.h"
#include "framework/adapters/outbound/hal/sim/hal_io_sim.h"
#include "framework/adapters/outbound/hal/sim/sim_encoder_counter.h"
#include "framework/ports/outbound/hal/hal_motor_port.h"
#include "framework/adapters/outbound/hal/components/motor_io/hal_motor_io_bind.h"
#include "framework/ports/outbound/hal/hal_io_port.h"
#include "framework/common/io_handle.h"
#include "framework/common/sw_error.h"
#include "unity.h"

#include <stdbool.h>
#include <stdint.h>

/* 有效仿真 IO 句柄 */
static const io_di_t k_di_cw_lim  = IO_DI(1U, 1U);
static const io_di_t k_di_ccw_lim = IO_DI(1U, 2U);

/* -------------------------------------------------------------------------
 * Mock 速度回调
 * ------------------------------------------------------------------------- */
static bool s_cb_set_speed_called = false;
static int  s_cb_speed_ref        = 0;

static sw_err_t mock_set_speed(int speed_ref, void *ctx)
{
    (void)ctx;
    s_cb_set_speed_called = true;
    s_cb_speed_ref        = speed_ref;
    return SW_OK;
}

static sw_err_t mock_read_current(uint16_t *p, void *ctx)
{
    (void)ctx;
    *p = 2000U;
    return SW_OK;
}

static sw_err_t mock_fault_reset(void *ctx)
{
    (void)ctx;
    return SW_OK;
}

/* 基础绑定（含编码器，无 VFD 回调）*/
static const hal_motor_io_bind_cfg_t k_base_cfg = {
    .limit_io_cw  = IO_DI(1U, 1U),
    .limit_io_ccw = IO_DI(1U, 2U),
    .has_encoder  = true,
};

void setUp(void)
{
    s_cb_set_speed_called = false;
    s_cb_speed_ref        = 0;

    hal_io_sim_register();      /* 重置 IO 仿真状态 */
    hal_motor_sim_register();   /* 清空槽位，注册 ops */
    hal_motor_sim_bind(0, &k_base_cfg);
}

void tearDown(void) {}

/* =========================================================================
 * A. 绑定参数校验
 * ========================================================================= */

static void test_bind_null_cfg(void)
{
    TEST_ASSERT_EQUAL_INT(SW_ERR_PARAM, hal_motor_sim_bind(0, NULL));
}

static void test_bind_negative_id(void)
{
    TEST_ASSERT_EQUAL_INT(SW_ERR_PARAM, hal_motor_sim_bind(-1, &k_base_cfg));
}

static void test_bind_id_out_of_range(void)
{
    TEST_ASSERT_EQUAL_INT(SW_ERR_PARAM,
        hal_motor_sim_bind(HAL_MOTOR_IO_SLOT_MAX, &k_base_cfg));
}

/* =========================================================================
 * B. 速度输出
 * ========================================================================= */

static void test_set_output_no_callback_returns_ok(void)
{
    /* 无 set_speed 回调：直接返回 SW_OK */
    TEST_ASSERT_EQUAL_INT(SW_OK, hal_motor_get_ops()->set_output(0, 1500));
}

static void test_set_output_with_callback(void)
{
    hal_motor_io_bind_cfg_t cfg = k_base_cfg;
    cfg.set_speed = mock_set_speed;
    hal_motor_sim_bind(0, &cfg);

    TEST_ASSERT_EQUAL_INT(SW_OK, hal_motor_get_ops()->set_output(0, 3000));
    TEST_ASSERT_TRUE(s_cb_set_speed_called);
    TEST_ASSERT_EQUAL_INT(3000, s_cb_speed_ref);
}

static void test_set_output_invalid_id(void)
{
    TEST_ASSERT_EQUAL_INT(SW_ERR_NOT_INIT,
        hal_motor_get_ops()->set_output(HAL_MOTOR_IO_SLOT_MAX, 0));
}

/* =========================================================================
 * C. 限位检测（依赖 hal_io_sim DI 状态）
 * ========================================================================= */

static void test_at_fwd_limit_true(void)
{
    hal_io_sim_set_di_level(k_di_cw_lim, true);
    TEST_ASSERT_TRUE(hal_motor_get_ops()->at_fwd_limit(0));
}

static void test_at_fwd_limit_false(void)
{
    hal_io_sim_set_di_level(k_di_cw_lim, false);
    TEST_ASSERT_FALSE(hal_motor_get_ops()->at_fwd_limit(0));
}

static void test_at_rev_limit_true(void)
{
    hal_io_sim_set_di_level(k_di_ccw_lim, true);
    TEST_ASSERT_TRUE(hal_motor_get_ops()->at_rev_limit(0));
}

/* =========================================================================
 * D. 编码器脉冲计数器（使用 sim_encoder_counter）
 * ========================================================================= */

static void test_read_hw_pulse_ok(void)
{
    uint32_t val = 0;
    sim_encoder_counter_add_pulse(0, 300);
    TEST_ASSERT_EQUAL_INT(SW_OK, hal_motor_get_ops()->read_hw_pulse(0, &val));
    TEST_ASSERT_EQUAL_UINT32(300U, val);
}

static void test_clear_hw_pulse_ok(void)
{
    uint32_t val = 99U;
    sim_encoder_counter_add_pulse(0, 100);
    TEST_ASSERT_EQUAL_INT(SW_OK, hal_motor_get_ops()->clear_hw_pulse(0));
    TEST_ASSERT_EQUAL_INT(SW_OK, hal_motor_get_ops()->read_hw_pulse(0, &val));
    TEST_ASSERT_EQUAL_UINT32(0U, val);
}

static void test_read_hw_pulse_no_encoder(void)
{
    uint32_t val;
    hal_motor_io_bind_cfg_t cfg = k_base_cfg;
    cfg.has_encoder = false;
    hal_motor_sim_bind(0, &cfg);
    TEST_ASSERT_EQUAL_INT(SW_ERR_PARAM,
        hal_motor_get_ops()->read_hw_pulse(0, &val));
}

/* =========================================================================
 * E. 电流 / 状态 / 故障复位回调
 * ========================================================================= */

static void test_read_current_no_callback(void)
{
    uint16_t val;
    TEST_ASSERT_EQUAL_INT(SW_ERR_NOT_SUPPORT,
        hal_motor_get_ops()->read_current(0, &val));
}

static void test_read_current_with_callback(void)
{
    uint16_t val = 0;
    hal_motor_io_bind_cfg_t cfg = k_base_cfg;
    cfg.read_current = mock_read_current;
    hal_motor_sim_bind(0, &cfg);

    TEST_ASSERT_EQUAL_INT(SW_OK, hal_motor_get_ops()->read_current(0, &val));
    TEST_ASSERT_EQUAL_UINT16(2000U, val);
}

static void test_fault_reset_no_callback(void)
{
    TEST_ASSERT_EQUAL_INT(SW_ERR_NOT_SUPPORT,
        hal_motor_get_ops()->fault_reset(0));
}

static void test_fault_reset_with_callback(void)
{
    hal_motor_io_bind_cfg_t cfg = k_base_cfg;
    cfg.fault_reset = mock_fault_reset;
    hal_motor_sim_bind(0, &cfg);

    TEST_ASSERT_EQUAL_INT(SW_OK, hal_motor_get_ops()->fault_reset(0));
}

/* =========================================================================
 * F. 补充场景
 * ========================================================================= */

static void test_at_rev_limit_false(void)
{
    hal_io_sim_set_di_level(k_di_ccw_lim, false);
    TEST_ASSERT_FALSE(hal_motor_get_ops()->at_rev_limit(0));
}

static void test_at_fwd_limit_null_di_returns_false(void)
{
    /* limit_io_cw = IO_HANDLE_NULL → 始终返回 false */
    hal_motor_io_bind_cfg_t cfg = k_base_cfg;
    cfg.limit_io_cw = (io_di_t){IO_HANDLE_NULL};
    hal_motor_sim_bind(0, &cfg);

    hal_io_sim_set_di_level(k_di_cw_lim, true); /* 物理上有信号，但未绑定 */
    TEST_ASSERT_FALSE(hal_motor_get_ops()->at_fwd_limit(0));
}

static void test_read_hw_pulse_null_ptr(void)
{
    TEST_ASSERT_EQUAL_INT(SW_ERR_PARAM,
        hal_motor_get_ops()->read_hw_pulse(0, NULL));
}

static void test_encoder_accumulates_multiple_adds(void)
{
    uint32_t val = 0;
    sim_encoder_counter_add_pulse(0, 100);
    sim_encoder_counter_add_pulse(0, 250);
    TEST_ASSERT_EQUAL_INT(SW_OK, hal_motor_get_ops()->read_hw_pulse(0, &val));
    TEST_ASSERT_EQUAL_UINT32(350U, val);
}

static void test_read_running_no_callback(void)
{
    bool is_running;
    TEST_ASSERT_EQUAL_INT(SW_ERR_NOT_SUPPORT,
        hal_motor_get_ops()->read_running(0, &is_running));
}

int main(void)
{
    UNITY_BEGIN();

    RUN_TEST(test_bind_null_cfg);
    RUN_TEST(test_bind_negative_id);
    RUN_TEST(test_bind_id_out_of_range);

    RUN_TEST(test_set_output_no_callback_returns_ok);
    RUN_TEST(test_set_output_with_callback);
    RUN_TEST(test_set_output_invalid_id);

    RUN_TEST(test_at_fwd_limit_true);
    RUN_TEST(test_at_fwd_limit_false);
    RUN_TEST(test_at_rev_limit_true);

    RUN_TEST(test_read_hw_pulse_ok);
    RUN_TEST(test_clear_hw_pulse_ok);
    RUN_TEST(test_read_hw_pulse_no_encoder);

    RUN_TEST(test_read_current_no_callback);
    RUN_TEST(test_read_current_with_callback);
    RUN_TEST(test_fault_reset_no_callback);
    RUN_TEST(test_fault_reset_with_callback);

    RUN_TEST(test_at_rev_limit_false);
    RUN_TEST(test_at_fwd_limit_null_di_returns_false);
    RUN_TEST(test_read_hw_pulse_null_ptr);
    RUN_TEST(test_encoder_accumulates_multiple_adds);
    RUN_TEST(test_read_running_no_callback);

    return UNITY_END();
}
