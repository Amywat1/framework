/**
 * @file    test_safety_output_hold.c
 * @brief   机构输出抑制：DI 电平与软件锁存
 */
#include "domain/safety/safety_output_hold.h"
#include "wdf_test_spec.h"

static bool s_di;

static bool di_reader(void)
{
    return s_di;
}

void setUp(void)
{
    s_di = false;
    safety_output_hold_reset();
}

void tearDown(void)
{
    safety_output_hold_reset();
}

static void test_request_latches_until_release(void)
{
    TEST_ASSERT_FALSE(safety_output_hold_is_active());
    safety_output_hold_request();
    TEST_ASSERT_TRUE(safety_output_hold_is_active());
    TEST_ASSERT_EQUAL_INT(SW_OK, safety_output_hold_release());
    TEST_ASSERT_FALSE(safety_output_hold_is_active());
}

static void test_di_samples_and_latches_after_inactive(void)
{
    safety_output_hold_bind_di(di_reader);
    s_di = true;
    TEST_ASSERT_TRUE(safety_output_hold_is_active());
    s_di = false;
    TEST_ASSERT_TRUE(safety_output_hold_is_active());
    TEST_ASSERT_EQUAL_INT(SW_OK, safety_output_hold_release());
    TEST_ASSERT_FALSE(safety_output_hold_is_active());
}

static void test_release_rejected_while_di_active(void)
{
    safety_output_hold_bind_di(di_reader);
    s_di = true;
    (void)safety_output_hold_is_active();
    TEST_ASSERT_EQUAL_INT(SW_ERR_STATE, safety_output_hold_release());
    s_di = false;
    TEST_ASSERT_EQUAL_INT(SW_OK, safety_output_hold_release());
    TEST_ASSERT_FALSE(safety_output_hold_is_active());
}

static void test_release_without_latch_is_ok(void)
{
    TEST_ASSERT_EQUAL_INT(SW_OK, safety_output_hold_release());
    TEST_ASSERT_FALSE(safety_output_hold_is_active());
}

int main(void)
{
    UNITY_BEGIN();
    WDF_RUN_TEST(test_request_latches_until_release, "SAFE-14", "验证软件锁存置位后保持到显式释放");
    WDF_RUN_TEST(test_di_samples_and_latches_after_inactive, "SAFE-14", "验证采样到急停 DI 后松开仍锁存");
    WDF_RUN_TEST(test_release_rejected_while_di_active, "SAFE-15", "验证急停 DI 有效时拒绝释放抑制");
    WDF_RUN_TEST(test_release_without_latch_is_ok, "SAFE-15", "验证未锁存时释放成功");
    return UNITY_END();
}
