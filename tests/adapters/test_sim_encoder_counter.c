/**
 * @file    test_sim_encoder_counter.c
 * @brief   sim_encoder_counter 仿真编码器计数器单元测试
 *
 * 分组：
 *   A. 累计与读取
 *   B. 清零与边界
 */

#include "adapters/outbound/hal/sim/sim_encoder_counter.h"
#include "common/sw_error.h"
#include "unity.h"

#include <stdint.h>

void setUp(void)
{
    sim_encoder_counter_reset_all();
}

void tearDown(void)
{
}

static void test_read_default_zero(void)
{
    uint32_t value = 99U;

    TEST_ASSERT_EQUAL_INT(SW_OK, sim_encoder_counter_read(0, &value));
    TEST_ASSERT_EQUAL_UINT32(0U, value);
}

static void test_add_pulse_positive(void)
{
    uint32_t value = 0U;

    sim_encoder_counter_add_pulse(1, 5);
    TEST_ASSERT_EQUAL_INT(SW_OK, sim_encoder_counter_read(1, &value));
    TEST_ASSERT_EQUAL_UINT32(5U, value);
}

static void test_add_pulse_negative_uses_absolute(void)
{
    uint32_t value = 0U;

    sim_encoder_counter_add_pulse(2, -7);
    TEST_ASSERT_EQUAL_INT(SW_OK, sim_encoder_counter_read(2, &value));
    TEST_ASSERT_EQUAL_UINT32(7U, value);
}

static void test_add_pulse_zero_delta_ignored(void)
{
    uint32_t value = 0U;

    sim_encoder_counter_add_pulse(0, 0);
    TEST_ASSERT_EQUAL_INT(SW_OK, sim_encoder_counter_read(0, &value));
    TEST_ASSERT_EQUAL_UINT32(0U, value);
}

static void test_clear_single_slot(void)
{
    uint32_t value = 0U;

    sim_encoder_counter_add_pulse(3, 10);
    TEST_ASSERT_EQUAL_INT(SW_OK, sim_encoder_counter_clear(3));
    TEST_ASSERT_EQUAL_INT(SW_OK, sim_encoder_counter_read(3, &value));
    TEST_ASSERT_EQUAL_UINT32(0U, value);
}

static void test_reset_all_clears_all_slots(void)
{
    uint32_t value = 0U;

    sim_encoder_counter_add_pulse(0, 1);
    sim_encoder_counter_add_pulse(1, 2);
    sim_encoder_counter_add_pulse(2, 3);
    sim_encoder_counter_add_pulse(3, 4);
    sim_encoder_counter_reset_all();

    for (int id = 0; id < 4; id++) {
        TEST_ASSERT_EQUAL_INT(SW_OK, sim_encoder_counter_read(id, &value));
        TEST_ASSERT_EQUAL_UINT32(0U, value);
    }
}

static void test_read_invalid_id(void)
{
    uint32_t value = 0U;

    TEST_ASSERT_EQUAL_INT(SW_ERR_PARAM, sim_encoder_counter_read(-1, &value));
    TEST_ASSERT_EQUAL_INT(SW_ERR_PARAM, sim_encoder_counter_read(4, &value));
}

static void test_read_null_pointer(void)
{
    TEST_ASSERT_EQUAL_INT(SW_ERR_PARAM, sim_encoder_counter_read(0, NULL));
}

static void test_clear_invalid_id(void)
{
    TEST_ASSERT_EQUAL_INT(SW_ERR_PARAM, sim_encoder_counter_clear(-1));
    TEST_ASSERT_EQUAL_INT(SW_ERR_PARAM, sim_encoder_counter_clear(4));
}

static void test_add_pulse_invalid_id_ignored(void)
{
    uint32_t value = 0U;

    sim_encoder_counter_add_pulse(-1, 5);
    sim_encoder_counter_add_pulse(4, 5);
    TEST_ASSERT_EQUAL_INT(SW_OK, sim_encoder_counter_read(0, &value));
    TEST_ASSERT_EQUAL_UINT32(0U, value);
}

int main(void)
{
    UNITY_BEGIN();

    RUN_TEST(test_read_default_zero);
    RUN_TEST(test_add_pulse_positive);
    RUN_TEST(test_add_pulse_negative_uses_absolute);
    RUN_TEST(test_add_pulse_zero_delta_ignored);
    RUN_TEST(test_clear_single_slot);
    RUN_TEST(test_reset_all_clears_all_slots);
    RUN_TEST(test_read_invalid_id);
    RUN_TEST(test_read_null_pointer);
    RUN_TEST(test_clear_invalid_id);
    RUN_TEST(test_add_pulse_invalid_id_ignored);

    return UNITY_END();
}
