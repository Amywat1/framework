/**
 * @file    test_hal_io_sim.c
 * @brief   hal_io_sim 数字 IO 仿真 HAL 单元测试
 *
 * 分组：
 *   A. DI 读写
 *   B. DO 输出
 *   C. 脉冲计数器
 *   D. 实用接口
 *   E. 边界场景
 */

#include "adapters/outbound/hal/sim/hal_io_sim.h"
#include "common/io_handle.h"
#include "common/sw_error.h"
#include "ports/outbound/hal/hal_io_port.h"
#include "wdf_test_spec.h"

#include <stdbool.h>
#include <stdint.h>

static const io_di_t k_valid_di = IO_DI(1U, 1U);
static const io_do_t k_valid_do = IO_DO(1U, 2U);
static const io_di_t k_bad_di   = IO_DI(0U, 1U);
static const io_do_t k_bad_do   = IO_DO(0U, 2U);

void setUp(void)
{
    hal_io_sim_test_reset();
    hal_io_sim_register();
    TEST_ASSERT_EQUAL_INT(SW_OK, hal_io_sim_validate_lifecycle());
    TEST_ASSERT_EQUAL_INT(SW_OK, hal_io_get_ops()->init());
    TEST_ASSERT_EQUAL_INT(SW_OK, hal_io_get_ops()->start());
}

void tearDown(void)
{
}

static void test_di_read_default_false(void)
{
    io_di_sample_t sample;

    TEST_ASSERT_EQUAL_INT(SW_OK, hal_io_get_ops()->di_read(k_valid_di, &sample));
    TEST_ASSERT_FALSE(sample.level);
    TEST_ASSERT_EQUAL_INT(IO_SAMPLE_QUALITY_VALID, sample.quality);
}

static void test_set_di_level_true_and_read(void)
{
    io_di_sample_t sample;

    hal_io_sim_set_di_level(k_valid_di, true);
    TEST_ASSERT_EQUAL_INT(SW_OK, hal_io_get_ops()->di_read(k_valid_di, &sample));
    TEST_ASSERT_TRUE(sample.level);
}

static void test_set_di_level_false_and_read(void)
{
    io_di_sample_t sample;

    hal_io_sim_set_di_level(k_valid_di, true);
    hal_io_sim_set_di_level(k_valid_di, false);
    TEST_ASSERT_EQUAL_INT(SW_OK, hal_io_get_ops()->di_read(k_valid_di, &sample));
    TEST_ASSERT_FALSE(sample.level);
}

static void test_di_invalid_board_returns_false(void)
{
    io_di_sample_t sample;

    hal_io_sim_set_di_level(k_bad_di, true);
    TEST_ASSERT_EQUAL_INT(SW_ERR_PARAM, hal_io_get_ops()->di_read(k_bad_di, &sample));
}

static void test_do_set_valid_returns_ok(void)
{
    TEST_ASSERT_EQUAL_INT(SW_OK, hal_io_get_ops()->do_set(k_valid_do, true));
}

static void test_do_level_tracks_output(void)
{
    bool level = false;

    TEST_ASSERT_EQUAL_INT(SW_OK, hal_io_get_ops()->do_set(k_valid_do, true));
    TEST_ASSERT_EQUAL_INT(SW_OK, hal_io_sim_get_do_level(k_valid_do, &level));
    TEST_ASSERT_TRUE(level);
    TEST_ASSERT_EQUAL_INT(SW_OK, hal_io_get_ops()->do_set(k_valid_do, false));
    TEST_ASSERT_EQUAL_INT(SW_OK, hal_io_sim_get_do_level(k_valid_do, &level));
    TEST_ASSERT_FALSE(level);
}

static void test_do_set_invalid_board_returns_err(void)
{
    TEST_ASSERT_EQUAL_INT(SW_ERR_PARAM, hal_io_get_ops()->do_set(k_bad_do, true));
}

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
    TEST_ASSERT_EQUAL_INT(SW_ERR_PARAM, hal_io_get_ops()->pulse_clear(k_bad_di));
}

static void test_pulse_counter_init_clears_value(void)
{
    hal_io_sim_set_pulse_counter(k_valid_di, 777U);
    TEST_ASSERT_EQUAL_INT(SW_OK, hal_io_get_ops()->init());
    TEST_ASSERT_EQUAL_INT(0, hal_io_get_ops()->pulse_read(k_valid_di));
}

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

static void test_di_pin_zero_returns_false(void)
{
    io_di_t        pin_zero = IO_DI(1U, 0U);
    io_di_sample_t sample;

    hal_io_sim_set_di_level(pin_zero, true);
    TEST_ASSERT_EQUAL_INT(SW_ERR_PARAM, hal_io_get_ops()->di_read(pin_zero, &sample));
}

static void test_di_board_max_boundary(void)
{
    io_di_t        pin_max_board = IO_DI(7U, 1U);
    io_di_sample_t sample;

    hal_io_sim_set_di_level(pin_max_board, true);
    TEST_ASSERT_EQUAL_INT(SW_OK, hal_io_get_ops()->di_read(pin_max_board, &sample));
    TEST_ASSERT_TRUE(sample.level);

    io_di_t pin_over_board = IO_DI(8U, 1U);
    hal_io_sim_set_di_level(pin_over_board, true);
    TEST_ASSERT_EQUAL_INT(SW_ERR_PARAM, hal_io_get_ops()->di_read(pin_over_board, &sample));
}

static void test_do_pin_zero_returns_err(void)
{
    io_do_t pin_zero = IO_DO(1U, 0U);
    TEST_ASSERT_EQUAL_INT(SW_ERR_PARAM, hal_io_get_ops()->do_set(pin_zero, true));
}

static void test_two_di_pins_independent(void)
{
    io_di_t        pin1 = IO_DI(1U, 1U);
    io_di_t        pin2 = IO_DI(1U, 2U);
    io_di_sample_t sample;

    hal_io_sim_set_di_level(pin1, true);
    hal_io_sim_set_di_level(pin2, false);

    TEST_ASSERT_EQUAL_INT(SW_OK, hal_io_get_ops()->di_read(pin1, &sample));
    TEST_ASSERT_TRUE(sample.level);
    TEST_ASSERT_EQUAL_INT(SW_OK, hal_io_get_ops()->di_read(pin2, &sample));
    TEST_ASSERT_FALSE(sample.level);
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

static void test_ops_before_init_return_not_init_or_safe_value(void)
{
    hal_io_stats_t stats;
    io_di_sample_t sample;

    hal_io_sim_test_reset();
    hal_io_sim_register();
    TEST_ASSERT_EQUAL_INT(SW_ERR_NOT_INIT, hal_io_get_ops()->start());
    TEST_ASSERT_EQUAL_INT(SW_ERR_NOT_INIT, hal_io_get_ops()->do_set(k_valid_do, true));
    TEST_ASSERT_EQUAL_INT(SW_ERR_NOT_INIT, hal_io_get_ops()->di_read(k_valid_di, &sample));
    TEST_ASSERT_EQUAL_INT(SW_ERR_NOT_INIT, hal_io_get_ops()->pulse_clear(k_valid_di));
    TEST_ASSERT_EQUAL_INT(SW_ERR_NOT_INIT, hal_io_get_ops()->get_stats(1, &stats));
    TEST_ASSERT_EQUAL_INT(SW_ERR_STATE, hal_io_sim_validate_lifecycle());
}

static void test_pulse_counter_max_value(void)
{
    io_di_t pin = IO_DI(1U, 5U);
    hal_io_sim_set_pulse_counter(pin, (uint32_t)0x7FFFFFFFU);
    TEST_ASSERT_EQUAL_INT((int)0x7FFFFFFFU, hal_io_get_ops()->pulse_read(pin));
}

static void test_adc_read_after_set(void)
{
    hal_io_sim_set_adc(1, 1, 100, 2500, 12);
    TEST_ASSERT_EQUAL_INT(100, hal_io_get_ops()->adc_read(1, 1));
    TEST_ASSERT_EQUAL_INT(2500, hal_io_get_ops()->adc_mv(1, 1));
    TEST_ASSERT_EQUAL_INT(12, hal_io_get_ops()->adc_ma(1, 1));
    TEST_ASSERT_EQUAL_INT(-1, hal_io_get_ops()->adc_read(1, 5));
    TEST_ASSERT_EQUAL_INT(-1, hal_io_get_ops()->adc_mv(0, 1));
}

static void test_di_quality_tracks_lifecycle(void)
{
    io_di_sample_t sample;

    hal_io_sim_test_reset();
    hal_io_sim_register();
    TEST_ASSERT_EQUAL_INT(SW_OK, hal_io_get_ops()->init());
    TEST_ASSERT_EQUAL_INT(SW_OK, hal_io_get_ops()->di_read(k_valid_di, &sample));
    TEST_ASSERT_EQUAL_INT(IO_SAMPLE_QUALITY_PROBING, sample.quality);

    TEST_ASSERT_EQUAL_INT(SW_OK, hal_io_get_ops()->start());
    TEST_ASSERT_EQUAL_INT(SW_OK, hal_io_get_ops()->di_read(k_valid_di, &sample));
    TEST_ASSERT_EQUAL_INT(IO_SAMPLE_QUALITY_VALID, sample.quality);

    hal_io_sim_set_board_online(1, false);
    TEST_ASSERT_EQUAL_INT(SW_OK, hal_io_get_ops()->di_read(k_valid_di, &sample));
    TEST_ASSERT_EQUAL_INT(IO_SAMPLE_QUALITY_OFFLINE, sample.quality);

    hal_io_sim_set_board_online(1, true);
    TEST_ASSERT_EQUAL_INT(SW_OK, hal_io_get_ops()->di_read(k_valid_di, &sample));
    TEST_ASSERT_EQUAL_INT(IO_SAMPLE_QUALITY_VALID, sample.quality);
}

int main(void)
{
    UNITY_BEGIN();

    WDF_RUN_TEST(test_di_read_default_false, "", "验证DI读取默认值false");
    WDF_RUN_TEST(test_set_di_level_true_and_read, "", "验证设置DI级别true并读取");
    WDF_RUN_TEST(test_set_di_level_false_and_read, "", "验证设置DI级别false并读取");
    WDF_RUN_TEST(test_di_invalid_board_returns_false, "", "验证DI无效板卡返回false");

    WDF_RUN_TEST(test_do_set_valid_returns_ok, "", "验证DO设置有效返回成功");
    WDF_RUN_TEST(test_do_level_tracks_output, "", "验证DO级别跟踪输出");
    WDF_RUN_TEST(test_do_set_invalid_board_returns_err, "", "验证DO设置无效板卡返回错误");

    WDF_RUN_TEST(test_pulse_read_after_set_counter, "", "验证设置脉冲计数器后可以读取新值");
    WDF_RUN_TEST(test_pulse_clear_zeros_counter, "", "验证脉冲清除归零计数器");
    WDF_RUN_TEST(test_pulse_read_invalid_pin_returns_negative, "", "验证脉冲读取无效引脚返回负数");
    WDF_RUN_TEST(test_pulse_clear_invalid_pin_returns_err, "", "验证脉冲清除无效引脚返回错误");
    WDF_RUN_TEST(test_pulse_counter_init_clears_value, "", "验证脉冲计数器初始化清除值");

    WDF_RUN_TEST(test_board_is_online_always_true, "", "验证模拟板卡始终报告在线");
    WDF_RUN_TEST(test_flush_outputs_returns_ok, "", "验证刷新输出返回成功");
    WDF_RUN_TEST(test_wait_boards_online_returns_ok, "", "验证等待板卡在线返回成功");
    WDF_RUN_TEST(test_get_stats_valid, "", "验证获取统计有效");
    WDF_RUN_TEST(test_get_stats_null_returns_err, "", "验证获取统计空指针返回错误");

    WDF_RUN_TEST(test_di_pin_zero_returns_false, "", "验证DI引脚零返回false");
    WDF_RUN_TEST(test_di_board_max_boundary, "", "验证DI板卡最大值边界");
    WDF_RUN_TEST(test_do_pin_zero_returns_err, "", "验证DO引脚零返回错误");
    WDF_RUN_TEST(test_two_di_pins_independent, "", "验证两个 DI 引脚状态相互独立");
    WDF_RUN_TEST(test_two_pulse_counters_independent, "", "验证两个脉冲计数器相互独立");
    WDF_RUN_TEST(test_start_returns_ok, "", "验证启动返回成功");
    WDF_RUN_TEST(test_ops_before_init_return_not_init_or_safe_value, "", "验证初始化前操作返回未初始化或安全值");
    WDF_RUN_TEST(test_pulse_counter_max_value, "", "验证脉冲计数器支持最大值");
    WDF_RUN_TEST(test_adc_read_after_set, "", "验证设置 ADC 值后可以读取");
    WDF_RUN_TEST(test_di_quality_tracks_lifecycle, "", "验证DI质量状态跟踪生命周期");

    return UNITY_END();
}
