/**
 * @file    test_observation_core.c
 * @brief   可观测核心与黑匣子单元测试
 */

#include "observability/core/observation.h"
#include "observability/recorder/blackbox_recorder.h"
#include "unity.h"

#include <stdint.h>
#include <string.h>

void setUp(void)
{
}

void tearDown(void)
{
}

static observation_record_spec_t make_record(observation_severity_t severity,
                                             uint32_t               event_code,
                                             const char            *text)
{
    observation_record_spec_t spec;

    memset(&spec, 0, sizeof(spec));
    spec.kind           = OBSERVATION_RECORD_EVENT;
    spec.severity       = severity;
    spec.payload_format = OBSERVATION_PAYLOAD_TEXT;
    spec.event_code     = event_code;
    spec.source         = "test";
    spec.payload        = text;
    spec.payload_size   = strlen(text);
    return spec;
}

static void test_observation_rejects_invalid_boot_id(void)
{
    TEST_ASSERT_EQUAL_INT(SW_ERR_PARAM, observation_init(0U));
}

static void test_observation_stamps_context_and_prioritizes_critical(void)
{
    observation_context_t     context = {.wash_session_id = 10U, .command_id = 20U, .correlation_id = 30U};
    observation_record_spec_t info    = make_record(OBSERVATION_SEVERITY_INFO, 100U, "info");
    observation_record_spec_t error   = make_record(OBSERVATION_SEVERITY_ERROR, 200U, "error");
    observation_record_t      out;

    TEST_ASSERT_EQUAL_INT(SW_OK, observation_init(1234U));
    observation_context_set(&context);
    TEST_ASSERT_EQUAL_INT(SW_OK, observation_publish(&info));
    TEST_ASSERT_EQUAL_INT(SW_OK, observation_publish(&error));

    TEST_ASSERT_EQUAL_INT(SW_OK, observation_try_pop(&out));
    TEST_ASSERT_EQUAL_UINT32(200U, out.event_code);
    TEST_ASSERT_EQUAL_UINT64(1234U, out.boot_id);
    TEST_ASSERT_EQUAL_UINT64(2U, out.sequence);
    TEST_ASSERT_EQUAL_UINT64(10U, out.context.wash_session_id);

    TEST_ASSERT_EQUAL_INT(SW_OK, observation_try_pop(&out));
    TEST_ASSERT_EQUAL_UINT32(100U, out.event_code);
    TEST_ASSERT_EQUAL_UINT64(1U, out.sequence);
    TEST_ASSERT_EQUAL_INT(SW_ERR_NOT_FOUND, observation_try_pop(&out));
}

static void test_observation_reports_normal_queue_overflow(void)
{
    observation_record_spec_t spec = make_record(OBSERVATION_SEVERITY_INFO, 300U, "normal");
    observation_stats_t       stats;
    uint32_t                  i;

    TEST_ASSERT_EQUAL_INT(SW_OK, observation_init(2U));
    for (i = 0U; i < OBSERVATION_NORMAL_QUEUE_CAPACITY; i++) {
        TEST_ASSERT_EQUAL_INT(SW_OK, observation_publish(&spec));
    }
    TEST_ASSERT_EQUAL_INT(SW_ERR_OVERFLOW, observation_publish(&spec));
    TEST_ASSERT_EQUAL_INT(SW_OK, observation_get_stats(&stats));
    TEST_ASSERT_EQUAL_UINT32(OBSERVATION_NORMAL_QUEUE_CAPACITY, stats.normal_queue_depth);
    TEST_ASSERT_EQUAL_UINT64(1U, stats.dropped_normal_full_count);
}

static void test_blackbox_preserves_configured_pre_and_post_window(void)
{
    blackbox_config_t        config = {.sample_rate_hz = 2U, .pre_trigger_seconds = 3U, .post_trigger_seconds = 2U};
    blackbox_snapshot_info_t info;
    blackbox_sample_t        sample;
    uint32_t                 value;

    TEST_ASSERT_EQUAL_INT(SW_OK, blackbox_recorder_init(&config));
    for (value = 0U; value < 8U; value++) {
        TEST_ASSERT_EQUAL_INT(SW_OK, blackbox_recorder_record(1U, &value, sizeof(value)));
    }
    TEST_ASSERT_EQUAL_INT(SW_OK, blackbox_recorder_trigger(99U, 400U));
    for (; value < 12U; value++) {
        TEST_ASSERT_EQUAL_INT(SW_OK, blackbox_recorder_record(1U, &value, sizeof(value)));
    }

    TEST_ASSERT_EQUAL_INT(SW_OK, blackbox_recorder_get_info(&info));
    TEST_ASSERT_EQUAL_INT(BLACKBOX_STATE_FROZEN, info.state);
    TEST_ASSERT_EQUAL_UINT32(10U, info.sample_count);
    TEST_ASSERT_EQUAL_INT(SW_OK, blackbox_recorder_copy_sample(0U, &sample));
    memcpy(&value, sample.payload, sizeof(value));
    TEST_ASSERT_EQUAL_UINT32(2U, value);
    TEST_ASSERT_EQUAL_INT(SW_OK, blackbox_recorder_copy_sample(9U, &sample));
    memcpy(&value, sample.payload, sizeof(value));
    TEST_ASSERT_EQUAL_UINT32(11U, value);
    TEST_ASSERT_EQUAL_INT(SW_OK, blackbox_recorder_release(99U));
}

static void test_blackbox_rejects_window_larger_than_capacity(void)
{
    blackbox_config_t config = {.sample_rate_hz = 100U, .pre_trigger_seconds = 30U, .post_trigger_seconds = 10U};

    TEST_ASSERT_EQUAL_INT(SW_ERR_PARAM, blackbox_recorder_init(&config));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_observation_rejects_invalid_boot_id);
    RUN_TEST(test_observation_stamps_context_and_prioritizes_critical);
    RUN_TEST(test_observation_reports_normal_queue_overflow);
    RUN_TEST(test_blackbox_preserves_configured_pre_and_post_window);
    RUN_TEST(test_blackbox_rejects_window_larger_than_capacity);
    return UNITY_END();
}
