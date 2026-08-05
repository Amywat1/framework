/**
 * @file    test_io_handle.c
 * @brief   io_handle 句柄编解码单元测试
 */

#include "common/io_handle.h"
#include "wdf_test_spec.h"

#include <stdint.h>

void setUp(void)
{
}

void tearDown(void)
{
}

static void test_di_encode_decode(void)
{
    io_di_t  pin = IO_DI(3U, 17U);
    uint16_t raw = io_di_raw(pin);

    TEST_ASSERT_EQUAL_UINT16(IO_KIND_DI, io_handle_kind(raw));
    TEST_ASSERT_EQUAL_UINT16(3U, io_handle_board(raw));
    TEST_ASSERT_EQUAL_UINT16(17U, io_handle_pin(raw));
}

static void test_do_encode_decode(void)
{
    io_do_t  pin = IO_DO(5U, 8U);
    uint16_t raw = io_do_raw(pin);

    TEST_ASSERT_EQUAL_UINT16(IO_KIND_DO, io_handle_kind(raw));
    TEST_ASSERT_EQUAL_UINT16(5U, io_handle_board(raw));
    TEST_ASSERT_EQUAL_UINT16(8U, io_handle_pin(raw));
}

static void test_di_do_kind_distinct(void)
{
    io_di_t di     = IO_DI(1U, 1U);
    io_do_t do_pin = IO_DO(1U, 1U);

    TEST_ASSERT_NOT_EQUAL(io_di_raw(di), io_do_raw(do_pin));
    TEST_ASSERT_EQUAL_UINT16(IO_KIND_DI, io_handle_kind(io_di_raw(di)));
    TEST_ASSERT_EQUAL_UINT16(IO_KIND_DO, io_handle_kind(io_do_raw(do_pin)));
}

static void test_make_helpers_match_macros(void)
{
    io_di_t macro_di  = IO_DI(2U, 10U);
    io_di_t helper_di = io_di_make(2U, 10U);
    io_do_t macro_do  = IO_DO(4U, 20U);
    io_do_t helper_do = io_do_make(4U, 20U);

    TEST_ASSERT_EQUAL_UINT16(io_di_raw(macro_di), io_di_raw(helper_di));
    TEST_ASSERT_EQUAL_UINT16(io_do_raw(macro_do), io_do_raw(helper_do));
}

static void test_board_pin_mask_boundary(void)
{
    io_di_t  pin = IO_DI(0x7FU, 0xFFU);
    uint16_t raw = io_di_raw(pin);

    TEST_ASSERT_EQUAL_UINT16(0x7FU, io_handle_board(raw));
    TEST_ASSERT_EQUAL_UINT16(0xFFU, io_handle_pin(raw));
}

static void test_board_overflow_is_masked(void)
{
    io_di_t  pin = IO_DI(0xFFU, 1U);
    uint16_t raw = io_di_raw(pin);

    TEST_ASSERT_EQUAL_UINT16(0x7FU, io_handle_board(raw));
}

int main(void)
{
    UNITY_BEGIN();

    WDF_RUN_TEST(test_di_encode_decode, "", "验证DI编码解码");
    WDF_RUN_TEST(test_do_encode_decode, "", "验证DO编码解码");
    WDF_RUN_TEST(test_di_do_kind_distinct, "", "验证DIDO类型互不相同");
    WDF_RUN_TEST(test_make_helpers_match_macros, "", "验证句柄构造辅助函数与宏结果一致");
    WDF_RUN_TEST(test_board_pin_mask_boundary, "", "验证板卡引脚掩码边界");
    WDF_RUN_TEST(test_board_overflow_is_masked, "", "验证超出范围的板卡编号按掩码截断");

    return UNITY_END();
}
