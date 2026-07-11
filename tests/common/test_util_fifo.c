/**
 * @file    test_util_fifo.c
 * @brief   util_fifo 单元测试
 */

#include "common/util_fifo.h"
#include "unity.h"

#include <string.h>

static uint8_t     s_buf[8];
static util_fifo_t s_fifo;

void setUp(void)
{
    (void)memset(s_buf, 0, sizeof(s_buf));
    (void)memset(&s_fifo, 0, sizeof(s_fifo));
}

void tearDown(void)
{
}

static void test_init_rejects_non_power_of_two(void)
{
    TEST_ASSERT_EQUAL_INT(SW_ERR_PARAM, util_fifo_init(&s_fifo, s_buf, 6U));
}

static void test_put_get_single_byte(void)
{
    uint8_t out = 0U;

    TEST_ASSERT_EQUAL_INT(SW_OK, util_fifo_init(&s_fifo, s_buf, 8U));
    TEST_ASSERT_EQUAL_INT(SW_OK, util_fifo_put(&s_fifo, 0xABU));
    TEST_ASSERT_EQUAL_INT(SW_OK, util_fifo_get(&s_fifo, &out));
    TEST_ASSERT_EQUAL_UINT8(0xABU, out);
}

static void test_get_empty_returns_timeout(void)
{
    uint8_t out = 0U;

    TEST_ASSERT_EQUAL_INT(SW_OK, util_fifo_init(&s_fifo, s_buf, 8U));
    TEST_ASSERT_EQUAL_INT(SW_ERR_TIMEOUT, util_fifo_get(&s_fifo, &out));
}

static void test_overflow_when_full(void)
{
    uint32_t i;

    TEST_ASSERT_EQUAL_INT(SW_OK, util_fifo_init(&s_fifo, s_buf, 8U));
    for (i = 0U; i < 8U; i++) {
        TEST_ASSERT_EQUAL_INT(SW_OK, util_fifo_put(&s_fifo, (uint8_t)i));
    }
    TEST_ASSERT_EQUAL_INT(SW_ERR_OVERFLOW, util_fifo_put(&s_fifo, 0xFFU));
}

static void test_write_read_bulk(void)
{
    const uint8_t src[]  = {1U, 2U, 3U, 4U};
    uint8_t       dst[4] = {0U};

    TEST_ASSERT_EQUAL_INT(SW_OK, util_fifo_init(&s_fifo, s_buf, 8U));
    TEST_ASSERT_EQUAL_UINT32(4U, util_fifo_write(&s_fifo, src, 4U));
    TEST_ASSERT_EQUAL_UINT32(4U, util_fifo_used(&s_fifo));
    TEST_ASSERT_EQUAL_UINT32(4U, util_fifo_read(&s_fifo, dst, 4U));
    TEST_ASSERT_EQUAL_UINT8_ARRAY(src, dst, 4);
}

static void test_flush_clears_data(void)
{
    TEST_ASSERT_EQUAL_INT(SW_OK, util_fifo_init(&s_fifo, s_buf, 8U));
    TEST_ASSERT_EQUAL_INT(SW_OK, util_fifo_put(&s_fifo, 0x11U));
    util_fifo_flush(&s_fifo);
    TEST_ASSERT_EQUAL_UINT32(0U, util_fifo_used(&s_fifo));
    TEST_ASSERT_EQUAL_UINT32(8U, util_fifo_free(&s_fifo));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_init_rejects_non_power_of_two);
    RUN_TEST(test_put_get_single_byte);
    RUN_TEST(test_get_empty_returns_timeout);
    RUN_TEST(test_overflow_when_full);
    RUN_TEST(test_write_read_bulk);
    RUN_TEST(test_flush_clears_data);
    return UNITY_END();
}
