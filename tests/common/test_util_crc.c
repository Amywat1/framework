/**
 * @file    test_util_crc.c
 * @brief   util_crc 单元测试
 */

#include "common/util_crc.h"
#include "wdf_test_spec.h"

#include <string.h>

static const uint8_t k_crc_sample[] = "123456789";

void setUp(void)
{
}

void tearDown(void)
{
}

static void test_crc32_known_vector(void)
{
    TEST_ASSERT_EQUAL_UINT32(0xCBF43926UL, util_crc32(k_crc_sample, (uint32_t)strlen((const char *)k_crc_sample)));
}

static void test_crc16_modbus_known_vector(void)
{
    TEST_ASSERT_EQUAL_UINT16(0x4B37U, util_crc16_modbus(k_crc_sample, (uint32_t)strlen((const char *)k_crc_sample)));
}

static void test_crc8_empty_input(void)
{
    TEST_ASSERT_EQUAL_UINT8(0x00U, util_crc8(k_crc_sample, 0U));
}

static void test_crc32_empty_input(void)
{
    TEST_ASSERT_EQUAL_UINT32(0x00000000UL, util_crc32(k_crc_sample, 0U));
}

static void test_crc16_single_byte(void)
{
    const uint8_t data[] = {0x01U};

    TEST_ASSERT_EQUAL_UINT16(32894U, util_crc16_modbus(data, 1U));
}

int main(void)
{
    UNITY_BEGIN();
    WDF_RUN_TEST(test_crc32_known_vector, "", "验证 CRC32 已知测试向量");
    WDF_RUN_TEST(test_crc16_modbus_known_vector, "", "验证 Modbus CRC16 已知测试向量");
    WDF_RUN_TEST(test_crc8_empty_input, "", "验证CRC8空输入");
    WDF_RUN_TEST(test_crc32_empty_input, "", "验证CRC32空输入");
    WDF_RUN_TEST(test_crc16_single_byte, "", "验证CRC16单个字节");
    return UNITY_END();
}
