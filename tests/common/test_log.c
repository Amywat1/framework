#include "unity.h"

#include "common/log.h"

void setUp(void)
{
}

void tearDown(void)
{
}

static void test_source_file_name_extracts_linux_path(void)
{
    TEST_ASSERT_EQUAL_STRING("motor_axis.c", sw_log_source_file_name("/opt/m8/framework/motor_axis.c"));
}

static void test_source_file_name_extracts_windows_path(void)
{
    TEST_ASSERT_EQUAL_STRING("motor_axis.c", sw_log_source_file_name("C:\\m8\\framework\\motor_axis.c"));
}

static void test_source_file_name_keeps_plain_name(void)
{
    TEST_ASSERT_EQUAL_STRING("motor_axis.c", sw_log_source_file_name("motor_axis.c"));
}

static void test_source_file_name_handles_null(void)
{
    TEST_ASSERT_EQUAL_STRING("", sw_log_source_file_name(NULL));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_source_file_name_extracts_linux_path);
    RUN_TEST(test_source_file_name_extracts_windows_path);
    RUN_TEST(test_source_file_name_keeps_plain_name);
    RUN_TEST(test_source_file_name_handles_null);
    return UNITY_END();
}
