#include "common/log.h"
#include "wdf_test_spec.h"

#include <stdarg.h>
#include <stddef.h>

static int            s_sink_calls;
static sw_log_level_t s_sink_last_level;

static void counting_sink(sw_log_level_t level, const char *component, const char *fmt, va_list ap)
{
    (void)component;
    (void)fmt;
    (void)ap;
    s_sink_calls++;
    s_sink_last_level = level;
}

void setUp(void)
{
    s_sink_calls      = 0;
    s_sink_last_level = SW_LOG_DEBUG;
    sw_log_register_sink(NULL);
    sw_log_set_level(SW_LOG_DEBUG);
}

void tearDown(void)
{
    sw_log_register_sink(NULL);
    sw_log_set_level(SW_LOG_DEBUG);
}

static void test_source_file_name_extracts_linux_path(void)
{
    TEST_ASSERT_EQUAL_STRING("motor_axis.c", sw_log_source_file_name("/opt/app/framework/motor_axis.c"));
}

static void test_source_file_name_extracts_windows_path(void)
{
    TEST_ASSERT_EQUAL_STRING("motor_axis.c", sw_log_source_file_name("C:\\app\\framework\\motor_axis.c"));
}

static void test_source_file_name_keeps_plain_name(void)
{
    TEST_ASSERT_EQUAL_STRING("motor_axis.c", sw_log_source_file_name("motor_axis.c"));
}

static void test_source_file_name_handles_null(void)
{
    TEST_ASSERT_EQUAL_STRING("", sw_log_source_file_name(NULL));
}

static void test_level_defaults_to_debug(void)
{
    TEST_ASSERT_EQUAL_INT(SW_LOG_DEBUG, sw_log_get_level());
    TEST_ASSERT_TRUE(sw_log_level_enabled(SW_LOG_DEBUG));
}

static void test_level_filter_drops_below_threshold(void)
{
    sw_log_register_sink(counting_sink);
    sw_log_set_level(SW_LOG_WARN);

    TEST_ASSERT_FALSE(sw_log_level_enabled(SW_LOG_INFO));
    TEST_ASSERT_FALSE(sw_log_level_enabled(SW_LOG_DEBUG));
    TEST_ASSERT_TRUE(sw_log_level_enabled(SW_LOG_WARN));
    TEST_ASSERT_TRUE(sw_log_level_enabled(SW_LOG_ERROR));

    sw_log_write(SW_LOG_DEBUG, "T", "dropped");
    sw_log_write(SW_LOG_INFO, "T", "dropped");
    TEST_ASSERT_EQUAL_INT(0, s_sink_calls);

    sw_log_write(SW_LOG_WARN, "T", "kept");
    TEST_ASSERT_EQUAL_INT(1, s_sink_calls);
    TEST_ASSERT_EQUAL_INT(SW_LOG_WARN, s_sink_last_level);

    sw_log_write(SW_LOG_ERROR, "T", "kept");
    TEST_ASSERT_EQUAL_INT(2, s_sink_calls);
}

static void test_level_error_only_keeps_error(void)
{
    sw_log_register_sink(counting_sink);
    sw_log_set_level(SW_LOG_ERROR);

    sw_log_write(SW_LOG_WARN, "T", "dropped");
    TEST_ASSERT_EQUAL_INT(0, s_sink_calls);
    sw_log_write(SW_LOG_ERROR, "T", "kept");
    TEST_ASSERT_EQUAL_INT(1, s_sink_calls);
}

static void test_set_level_rejects_out_of_range(void)
{
    sw_log_set_level(SW_LOG_WARN);
    sw_log_set_level((sw_log_level_t)99);
    TEST_ASSERT_EQUAL_INT(SW_LOG_WARN, sw_log_get_level());

    sw_log_set_level((sw_log_level_t)-1);
    TEST_ASSERT_EQUAL_INT(SW_LOG_WARN, sw_log_get_level());
}

int main(void)
{
    UNITY_BEGIN();
    WDF_RUN_TEST(test_source_file_name_extracts_linux_path, "", "验证源文件名称提取LINUX路径");
    WDF_RUN_TEST(test_source_file_name_extracts_windows_path, "", "验证源文件名称提取WINDOWS路径");
    WDF_RUN_TEST(test_source_file_name_keeps_plain_name, "", "验证源文件名称保持普通名称");
    WDF_RUN_TEST(test_source_file_name_handles_null, "", "验证源文件名称处理空指针");
    WDF_RUN_TEST(test_level_defaults_to_debug, "", "验证日志级别默认使用调试级");
    WDF_RUN_TEST(test_level_filter_drops_below_threshold, "", "验证级别过滤丢弃低于阈值");
    WDF_RUN_TEST(test_level_error_only_keeps_error, "", "验证级别错误仅保持错误");
    WDF_RUN_TEST(test_set_level_rejects_out_of_range, "", "验证日志拒绝范围外的级别");
    return UNITY_END();
}
