#include "unity.h"

#include "common/log.h"

#include <stdio.h>
#include <string.h>

#define TEST_MESSAGE_SIZE 64U

static char     s_first_message[TEST_MESSAGE_SIZE];
static char     s_second_message[TEST_MESSAGE_SIZE];
static unsigned s_first_count;
static unsigned s_second_count;

static void first_sink(sw_log_level_t level, const char *fmt, va_list ap)
{
    (void)level;
    (void)vsnprintf(s_first_message, sizeof(s_first_message), fmt, ap);
    s_first_count++;
}

static void second_sink(sw_log_level_t level, const char *fmt, va_list ap)
{
    (void)level;
    (void)vsnprintf(s_second_message, sizeof(s_second_message), fmt, ap);
    s_second_count++;
}

void setUp(void)
{
    memset(s_first_message, 0, sizeof(s_first_message));
    memset(s_second_message, 0, sizeof(s_second_message));
    s_first_count = 0U;
    s_second_count = 0U;
    sw_log_register_sink(NULL);
}

void tearDown(void)
{
}

static void test_log_dispatches_to_all_sinks(void)
{
    TEST_ASSERT_TRUE(sw_log_add_sink(first_sink));
    TEST_ASSERT_TRUE(sw_log_add_sink(second_sink));
    sw_log_write(SW_LOG_INFO, "value=%d", 7);
    TEST_ASSERT_EQUAL_UINT(1U, s_first_count);
    TEST_ASSERT_EQUAL_UINT(1U, s_second_count);
    TEST_ASSERT_EQUAL_STRING("value=7", s_first_message);
    TEST_ASSERT_EQUAL_STRING("value=7", s_second_message);
}

static void test_log_ignores_duplicate_sink(void)
{
    TEST_ASSERT_TRUE(sw_log_add_sink(first_sink));
    TEST_ASSERT_TRUE(sw_log_add_sink(first_sink));
    sw_log_write(SW_LOG_INFO, "duplicate");
    TEST_ASSERT_EQUAL_UINT(1U, s_first_count);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_log_dispatches_to_all_sinks);
    RUN_TEST(test_log_ignores_duplicate_sink);
    return UNITY_END();
}
