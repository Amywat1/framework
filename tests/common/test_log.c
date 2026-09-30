#include "common/log.h"
#include "wdf_test_spec.h"

#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

enum { LOG_CAPTURE_LEN = SW_LOG_LINE_MAX };

static int             s_sink_calls;
static sw_log_level_t  s_sink_last_level;
static char            s_captured[LOG_CAPTURE_LEN];
static bool            s_seen_occurred;
static struct timespec s_sink_occurred;

static void counting_sink(sw_log_level_t level, const char *component, const char *fmt, va_list ap)
{
    (void)component;
    (void)fmt;
    (void)ap;
    s_sink_calls++;
    s_sink_last_level = level;
}

static void capture_sink(sw_log_level_t level, const char *component, const char *fmt, va_list ap)
{
    (void)level;
    (void)component;
    (void)vsnprintf(s_captured, sizeof(s_captured), fmt, ap);
    s_sink_calls++;
}

static uint64_t timespec_to_ms(const struct timespec *ts)
{
    return (uint64_t)ts->tv_sec * 1000ULL + (uint64_t)ts->tv_nsec / 1000000ULL;
}

static void occurred_sink(sw_log_level_t level, const char *component, const char *fmt, va_list ap)
{
    (void)level;
    (void)component;
    (void)fmt;
    (void)ap;
    s_seen_occurred = sw_log_occurred_at(&s_sink_occurred);
    s_sink_calls++;
}

void setUp(void)
{
    s_sink_calls      = 0;
    s_sink_last_level = SW_LOG_DEBUG;
    s_seen_occurred   = false;
    (void)memset(s_captured, 0, sizeof(s_captured));
    (void)memset(&s_sink_occurred, 0, sizeof(s_sink_occurred));
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

/** @brief 未超出容量时保持原文 */
static void test_clip_line_keeps_short(void)
{
    char line[16];
    int  written;

    written = snprintf(line, sizeof(line), "hello");
    TEST_ASSERT_EQUAL_UINT(5U, sw_log_clip_line(line, sizeof(line), written));
    TEST_ASSERT_EQUAL_STRING("hello", line);
}

/** @brief 超出容量时按实测长度把行尾换成截断标记 */
static void test_clip_line_replaces_tail(void)
{
    char   line[32];
    size_t mark_len = sizeof(SW_LOG_TRUNCATED_MARK) - 1U;

    (void)memset(line, 'A', sizeof(line) - 1U);
    line[sizeof(line) - 1U] = '\0';
    TEST_ASSERT_EQUAL_UINT(sizeof(line) - 1U, sw_log_clip_line(line, sizeof(line), 64));
    TEST_ASSERT_EQUAL_UINT(sizeof(line) - 1U, strlen(line));
    TEST_ASSERT_EQUAL_STRING(SW_LOG_TRUNCATED_MARK, line + (sizeof(line) - 1U - mark_len));
}

/** @brief 超过 SW_LOG_LINE_MAX 的正文单行收口，行尾为截断标记 */
static void test_long_message_clips_tail(void)
{
    char   big[SW_LOG_LINE_MAX + 80U];
    size_t mark_len = sizeof(SW_LOG_TRUNCATED_MARK) - 1U;

    (void)memset(big, 'A', sizeof(big) - 1U);
    big[sizeof(big) - 1U] = '\0';

    sw_log_register_sink(capture_sink);
    sw_log_write(SW_LOG_INFO, "T", "%s", big);

    TEST_ASSERT_EQUAL_INT(1, s_sink_calls);
    TEST_ASSERT_EQUAL_UINT(SW_LOG_LINE_MAX - 1U, strlen(s_captured));
    TEST_ASSERT_EQUAL_STRING(SW_LOG_TRUNCATED_MARK, s_captured + (SW_LOG_LINE_MAX - 1U - mark_len));
}

static void test_occurred_at_unavailable_outside_sink(void)
{
    struct timespec ts;

    TEST_ASSERT_FALSE(sw_log_occurred_at(NULL));
    TEST_ASSERT_FALSE(sw_log_occurred_at(&ts));
}

static void test_sync_sink_sees_call_site_time(void)
{
    struct timespec before;
    struct timespec after;

    TEST_ASSERT_EQUAL_INT(0, clock_gettime(CLOCK_REALTIME, &before));
    sw_log_register_sink(occurred_sink);
    sw_log_write(SW_LOG_INFO, "T", "sync-time");
    TEST_ASSERT_EQUAL_INT(0, clock_gettime(CLOCK_REALTIME, &after));

    TEST_ASSERT_EQUAL_INT(1, s_sink_calls);
    TEST_ASSERT_TRUE(s_seen_occurred);
    TEST_ASSERT_TRUE(timespec_to_ms(&s_sink_occurred) >= timespec_to_ms(&before));
    TEST_ASSERT_TRUE(timespec_to_ms(&s_sink_occurred) <= timespec_to_ms(&after));
    TEST_ASSERT_FALSE(sw_log_occurred_at(&after));
}

static void test_format_occurred_at_keeps_millis(void)
{
    struct timespec ts;
    char            buf[40];

    ts.tv_sec  = 1710000000;
    ts.tv_nsec = 123000000;
    TEST_ASSERT_EQUAL_UINT(0U, sw_log_format_occurred_at(&ts, NULL, sizeof(buf), false));
    TEST_ASSERT_TRUE(sw_log_format_occurred_at(&ts, buf, sizeof(buf), false) > 0U);
    TEST_ASSERT_NOT_NULL(strstr(buf, ".123"));
    TEST_ASSERT_TRUE(sw_log_format_occurred_at(&ts, buf, sizeof(buf), true) > 0U);
    TEST_ASSERT_NOT_NULL(strstr(buf, ".123"));
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
    WDF_RUN_TEST(test_clip_line_keeps_short, "", "验证短行按实测长度保持原文");
    WDF_RUN_TEST(test_clip_line_replaces_tail, "", "验证超长行按实测长度替换行尾");
    WDF_RUN_TEST(test_long_message_clips_tail, "", "验证超长日志行尾截断");
    WDF_RUN_TEST(test_occurred_at_unavailable_outside_sink, "LOG-07", "验证 sink 外读取发生时刻失败");
    WDF_RUN_TEST(test_sync_sink_sees_call_site_time, "LOG-07", "验证同步写出时 sink 看到调用点时刻");
    WDF_RUN_TEST(test_format_occurred_at_keeps_millis, "LOG-07", "验证发生时刻格式化保留毫秒");
    return UNITY_END();
}
