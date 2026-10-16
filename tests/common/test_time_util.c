/**
 * @file    test_time_util.c
 * @brief   单调时钟与 Host 时间倍率单测
 */

#include "common/time_util.h"

#include "unity.h"

#include <stdlib.h>
#include <unistd.h>

void setUp(void)
{
    (void)unsetenv("WDF_TIME_SCALE");
    time_util_reset_for_test();
}

void tearDown(void)
{
    (void)unsetenv("WDF_TIME_SCALE");
    time_util_reset_for_test();
}

void test_default_scale_is_one(void)
{
    TEST_ASSERT_EQUAL_UINT32(1U, time_util_scale());
    TEST_ASSERT_EQUAL_UINT32(40U, time_util_to_wall_ms(40U));
    TEST_ASSERT_EQUAL_UINT32(0U, time_util_to_wall_ms(0U));
}

void test_scale_divides_wall_ms(void)
{
    TEST_ASSERT_EQUAL_INT(0, setenv("WDF_TIME_SCALE", "5", 1));
    time_util_reset_for_test();

    TEST_ASSERT_EQUAL_UINT32(5U, time_util_scale());
    TEST_ASSERT_EQUAL_UINT32(8U, time_util_to_wall_ms(40U));
    TEST_ASSERT_EQUAL_UINT32(2U, time_util_to_wall_ms(10U));
    TEST_ASSERT_EQUAL_UINT32(1U, time_util_to_wall_ms(1U));
}

void test_invalid_scale_falls_back_to_one(void)
{
    TEST_ASSERT_EQUAL_INT(0, setenv("WDF_TIME_SCALE", "4", 1));
    time_util_reset_for_test();
    TEST_ASSERT_EQUAL_UINT32(1U, time_util_scale());

    TEST_ASSERT_EQUAL_INT(0, setenv("WDF_TIME_SCALE", "8", 1));
    time_util_reset_for_test();
    TEST_ASSERT_EQUAL_UINT32(1U, time_util_scale());

    TEST_ASSERT_EQUAL_INT(0, setenv("WDF_TIME_SCALE", "99", 1));
    time_util_reset_for_test();
    TEST_ASSERT_EQUAL_UINT32(1U, time_util_scale());

    TEST_ASSERT_EQUAL_INT(0, setenv("WDF_TIME_SCALE", "abc", 1));
    time_util_reset_for_test();
    TEST_ASSERT_EQUAL_UINT32(1U, time_util_scale());
}

void test_get_ms_advances_with_scale(void)
{
    uint64_t before;
    uint64_t after;
    uint32_t elapsed;

    TEST_ASSERT_EQUAL_INT(0, setenv("WDF_TIME_SCALE", "5", 1));
    time_util_reset_for_test();

    before = time_util_get_ms();
    (void)usleep(50000U);
    after   = time_util_get_ms();
    elapsed = time_elapsed_ms(before, after);

    TEST_ASSERT_TRUE(elapsed >= 180U);
    TEST_ASSERT_TRUE(elapsed < 600U);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_default_scale_is_one);
    RUN_TEST(test_scale_divides_wall_ms);
    RUN_TEST(test_invalid_scale_falls_back_to_one);
    RUN_TEST(test_get_ms_advances_with_scale);
    return UNITY_END();
}
