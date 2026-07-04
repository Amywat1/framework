/**
 * @file    test_hal_io_sim.c
 * @brief   hal_io_sim 数字 IO 仿真 HAL 单元测试
 *
 * 分组�?
 *   A. DI 读写
 *   B. DO 输出
 *   C. 脉冲计数�?
 *   D. 实用接口
 */

#include "framework/adapters/outbound/hal/sim/hal_io_sim.h"
#include "framework/ports/outbound/hal/hal_io_port.h"
#include "framework/common/io_handle.h"
#include "framework/common/sw_error.h"
#include "unity.h"

#include <stdbool.h>
#include <stdint.h>

/* 有效仿真句柄：board=1�?0, <SIM_IO_BOARD_MAX=8），pin=1�?0, <=32�?/
static const io_di_t k_valid_di = IO_DI(1U, 1U);
static const io_do_t k_valid_do = IO_DO(1U, 2U);

/* board=0 为无效（sim 要求 board>0�?/
static const io_di_t k_bad_di   = IO_DI(0U, 1U);
static const io_do_t k_bad_do   = IO_DO(0U, 2U);

void setUp(void)
{
    /* register 会调�?sim_io_init() 清空全部状�?*/
    hal_io_sim_register();
}

void tearDown(void) {}

/* =========================================================================
 * A. DI 读写
 * ========================================================================= */

static void test_di_read_default_false(void)
{
    TEST_ASSERT_FALSE(hal_io_get_ops()->di_read(k_valid_di));
}

static void test_set_di_level_true_and_read(void)
{
    hal_io_sim_set_di_level(k_valid_di, true);
    TEST_ASSERT_TRUE(hal_io_get_ops()->di_read(k_valid_di));
}

static void test_set_di_level_false_and_read(void)
{
    hal_io_sim_set_di_level(k_valid_di, true);
    hal_io_sim_set_di_level(k_valid_di, false);
    TEST_ASSERT_FALSE(hal_io_get_ops()->di_read(k_valid_di));
}

static void test_di_invalid_board_returns_false(void)
{
    hal_io_sim_set_di_level(k_bad_di, true); /* 无效句柄，静默忽�?*/
    TEST_ASSERT_FALSE(hal_io_get_ops()->di_read(k_bad_di));
}

/* =========================================================================
 * B. DO 输出
 * ========================================================================= */

static void test_do_set_valid_returns_ok(void)
{
    TEST_ASSERT_EQUAL_INT(SW_OK, hal_io_get_ops()->do_set(k_valid_do, true));
}

static void test_do_set_invalid_board_returns_err(void)
{
    TEST_ASSERT_EQUAL_INT(SW_ERR_PARAM, hal_io_get_ops()->do_set(k_bad_do, true));
}

/* =========================================================================
 * C. 脉冲计数�?
 * ========================================================================= */

static void test_pulse_read_after_set_counter(void)
{
    hal_io_sim_set_pulse_counter(k_valid_di, 500U);
    TEST_ASSERT_EQUAL_INT(500, hal_io_get_ops()->pulse_read(k_valid_di));
}

static void test_pulse_clear_zeros_counter(void)
{
    hal_io_sim_set_pulse_counter(k_valid_di, 999U);
    TEST_ASSERT_EQUAL_INT(SW_OK, hal_io_get_ops()->pulse_clear(k_valid_di));
    TEST_ASSERT_EQUAL_INT(0, hal_io_get_ops()->pulse_read(k_valid_di));
}

static void test_pulse_read_invalid_pin_returns_negative(void)
{
    TEST_ASSERT_TRUE(hal_io_get_ops()->pulse_read(k_bad_di) < 0);
}

static void test_pulse_clear_invalid_pin_returns_err(void)
{
    TEST_ASSERT_EQUAL_INT(SW_ERR_PARAM,
        hal_io_get_ops()->pulse_clear(k_bad_di));
}

static void test_pulse_counter_init_clears_value(void)
{
    hal_io_sim_set_pulse_counter(k_valid_di, 777U);
    /* 重新 register 会调�?sim_io_init()，清空所有计数器 */
    hal_io_sim_register();
    TEST_ASSERT_EQUAL_INT(0, hal_io_get_ops()->pulse_read(k_valid_di));
}

/* =========================================================================
 * D. 实用接口
 * ========================================================================= */

static void test_board_is_online_always_true(void)
{
    TEST_ASSERT_TRUE(hal_io_get_ops()->board_is_online(1));
}

static void test_flush_outputs_returns_ok(void)
{
    TEST_ASSERT_EQUAL_INT(SW_OK, hal_io_get_ops()->flush_outputs_now());
}

static void test_wait_boards_online_returns_ok(void)
{
    TEST_ASSERT_EQUAL_INT(SW_OK, hal_io_get_ops()->wait_boards_online(100U));
}

static void test_get_stats_valid(void)
{
    hal_io_stats_t stats;
    TEST_ASSERT_EQUAL_INT(SW_OK, hal_io_get_ops()->get_stats(1, &stats));
    TEST_ASSERT_TRUE(stats.online);
}

static void test_get_stats_null_returns_err(void)
{
    TEST_ASSERT_EQUAL_INT(SW_ERR_PARAM, hal_io_get_ops()->get_stats(1, NULL));
}

/* =========================================================================
 * E. 补充边界场景
 * ========================================================================= */

static void test_di_pin_zero_returns_false(void)
{
    /* pin=0 无效（sim 要求 pin > 0�?*/
    io_di_t pin_zero = IO_DI(1U, 0U);
    hal_io_sim_set_di_level(pin_zero, true); /* 静默忽略 */
    TEST_ASSERT_FALSE(hal_io_get_ops()->di_read(pin_zero));
}

static void test_di_board_max_boundary(void)
{
    /* board=7（SIM_IO_BOARD_MAX-1）为合法最大板�?*/
    io_di_t pin_max_board = IO_DI(7U, 1U);
    hal_io_sim_set_di_level(pin_max_board, true);
    TEST_ASSERT_TRUE(hal_io_get_ops()->di_read(pin_max_board));

    /* board=8 >= SIM_IO_BOARD_MAX，无�?*/
    io_di_t pin_over_board = IO_DI(8U, 1U);
    hal_io_sim_set_di_level(pin_over_board, true);
    TEST_ASSERT_FALSE(hal_io_get_ops()->di_read(pin_over_board));
}

static void test_do_pin_zero_returns_err(void)
{
    io_do_t pin_zero = IO_DO(1U, 0U);
    TEST_ASSERT_EQUAL_INT(SW_ERR_PARAM, hal_io_get_ops()->do_set(pin_zero, true));
}

static void test_two_di_pins_independent(void)
{
    io_di_t pin1 = IO_DI(1U, 1U);
    io_di_t pin2 = IO_DI(1U, 2U);

    hal_io_sim_set_di_level(pin1, true);
    hal_io_sim_set_di_level(pin2, false);

    TEST_ASSERT_TRUE(hal_io_get_ops()->di_read(pin1));
    TEST_ASSERT_FALSE(hal_io_get_ops()->di_read(pin2));
}

static void test_two_pulse_counters_independent(void)
{
    io_di_t pin1 = IO_DI(1U, 1U);
    io_di_t pin2 = IO_DI(1U, 2U);

    hal_io_sim_set_pulse_counter(pin1, 100U);
    hal_io_sim_set_pulse_counter(pin2, 200U);

    TEST_ASSERT_EQUAL_INT(100, hal_io_get_ops()->pulse_read(pin1));
    TEST_ASSERT_EQUAL_INT(200, hal_io_get_ops()->pulse_read(pin2));
}

static void test_start_returns_ok(void)
{
    TEST_ASSERT_EQUAL_INT(SW_OK, hal_io_get_ops()->start());
}

static void test_pulse_counter_max_value(void)
{
    /* uint32 最大值存储后应原样读出（截断�?int 前先验证无溢出崩溃）*/
    io_di_t pin = IO_DI(1U, 5U);
    hal_io_sim_set_pulse_counter(pin, (uint32_t)0x7FFFFFFFU); /* INT_MAX */
    TEST_ASSERT_EQUAL_INT((int)0x7FFFFFFFU, hal_io_get_ops()->pulse_read(pin));
}

int main(void)
{
    UNITY_BEGIN();

    RUN_TEST(test_di_read_default_false);
    RUN_TEST(test_set_di_level_true_and_read);
    RUN_TEST(test_set_di_level_false_and_read);
    RUN_TEST(test_di_invalid_board_returns_false);

    RUN_TEST(test_do_set_valid_returns_ok);
    RUN_TEST(test_do_set_invalid_board_returns_err);

    RUN_TEST(test_pulse_read_after_set_counter);
    RUN_TEST(test_pulse_clear_zeros_counter);
    RUN_TEST(test_pulse_read_invalid_pin_returns_negative);
    RUN_TEST(test_pulse_clear_invalid_pin_returns_err);
    RUN_TEST(test_pulse_counter_init_clears_value);

    RUN_TEST(test_board_is_online_always_true);
    RUN_TEST(test_flush_outputs_returns_ok);
    RUN_TEST(test_wait_boards_online_returns_ok);
    RUN_TEST(test_get_stats_valid);
    RUN_TEST(test_get_stats_null_returns_err);

    RUN_TEST(test_di_pin_zero_returns_false);
    RUN_TEST(test_di_board_max_boundary);
    RUN_TEST(test_do_pin_zero_returns_err);
    RUN_TEST(test_two_di_pins_independent);
    RUN_TEST(test_two_pulse_counters_independent);
    RUN_TEST(test_start_returns_ok);
    RUN_TEST(test_pulse_counter_max_value);

    return UNITY_END();
}
