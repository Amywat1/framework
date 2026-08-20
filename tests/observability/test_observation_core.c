/**
 * @file    test_observation_core.c
 * @brief   可观测核心与黑匣子单元测试
 */

#include "observability/core/observation.h"
#include "observability/recorder/blackbox_recorder.h"
#include "wdf_test_spec.h"

#include <stdint.h>
#include <string.h>

void setUp(void)
{
}

void tearDown(void)
{
}

static observation_record_spec_t make_record(observation_severity_t severity, uint32_t event_code, const char *text)
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

/* 就绪查询必须与 publish 的可用性一致：bootstrap 用它决定是否挂事件桥接，
 * 若非法 boot_id 被误判为就绪，桥接会挂上去而所有记录都被 NOT_INIT 丢掉。 */
static void test_observation_ready_tracks_init(void)
{
    observation_record_spec_t rec = make_record(OBSERVATION_SEVERITY_INFO, 1U, "x");

    TEST_ASSERT_EQUAL_INT(SW_ERR_PARAM, observation_init(0U));
    TEST_ASSERT_FALSE(observation_is_ready());
    TEST_ASSERT_EQUAL_INT(SW_ERR_NOT_INIT, observation_publish(&rec));

    TEST_ASSERT_EQUAL_INT(SW_OK, observation_init(77U));
    TEST_ASSERT_TRUE(observation_is_ready());
    TEST_ASSERT_EQUAL_INT(SW_OK, observation_publish(&rec));
}

static void test_observation_stamps_context_and_prioritizes_critical(void)
{
    observation_context_t     context = {.wash_session_id = 10U, .command_id = 20U, .correlation_id = 30U};
    observation_record_spec_t info    = make_record(OBSERVATION_SEVERITY_INFO, 100U, "info");
    observation_record_spec_t error   = make_record(OBSERVATION_SEVERITY_ERROR, 200U, "error");
    observation_record_t      out;

    TEST_ASSERT_EQUAL_INT(SW_OK, observation_init(1234U));
    observation_set_export_available(true);
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
    observation_set_export_available(true);
    for (i = 0U; i < OBSERVATION_NORMAL_QUEUE_CAPACITY; i++) {
        TEST_ASSERT_EQUAL_INT(SW_OK, observation_publish(&spec));
    }
    TEST_ASSERT_EQUAL_INT(SW_ERR_OVERFLOW, observation_publish(&spec));
    TEST_ASSERT_EQUAL_INT(SW_OK, observation_get_stats(&stats));
    TEST_ASSERT_EQUAL_UINT32(OBSERVATION_NORMAL_QUEUE_CAPACITY, stats.normal_queue_depth);
    TEST_ASSERT_EQUAL_UINT64(1U, stats.dropped_normal_full_count);
}

static void test_observation_offline_keeps_only_latest_and_reliable(void)
{
    observation_record_spec_t normal   = make_record(OBSERVATION_SEVERITY_INFO, 301U, "normal");
    observation_record_spec_t latest   = make_record(OBSERVATION_SEVERITY_INFO, 302U, "old");
    observation_record_spec_t reliable = make_record(OBSERVATION_SEVERITY_WARN, 303U, "critical");
    observation_record_t      out;
    observation_stats_t       stats;

    latest.delivery   = OBSERVATION_DELIVERY_LATEST;
    reliable.delivery = OBSERVATION_DELIVERY_RELIABLE;
    TEST_ASSERT_EQUAL_INT(SW_OK, observation_init(3U));
    observation_set_export_available(false);
    TEST_ASSERT_EQUAL_INT(SW_ERR_OVERFLOW, observation_publish(&normal));
    TEST_ASSERT_EQUAL_INT(SW_OK, observation_publish(&latest));
    latest.payload      = "new";
    latest.payload_size = 3U;
    TEST_ASSERT_EQUAL_INT(SW_OK, observation_publish(&latest));
    TEST_ASSERT_EQUAL_INT(SW_OK, observation_publish(&reliable));

    TEST_ASSERT_EQUAL_INT(SW_OK, observation_try_pop(&out));
    TEST_ASSERT_EQUAL_UINT32(302U, out.event_code);
    TEST_ASSERT_EQUAL_MEMORY("new", out.payload, 3U);
    TEST_ASSERT_EQUAL_INT(SW_OK, observation_try_pop(&out));
    TEST_ASSERT_EQUAL_UINT32(303U, out.event_code);
    TEST_ASSERT_EQUAL_INT(SW_ERR_NOT_FOUND, observation_try_pop(&out));
    TEST_ASSERT_EQUAL_INT(SW_OK, observation_get_stats(&stats));
    TEST_ASSERT_EQUAL_UINT64(1U, stats.dropped_offline_count);
    TEST_ASSERT_EQUAL_UINT64(1U, stats.latest_replaced_count);
}

static void test_observation_coalesces_repeated_reliable_logs(void)
{
    observation_record_spec_t log = make_record(OBSERVATION_SEVERITY_WARN, 304U, "same warning");
    observation_record_t      out;
    observation_stats_t       stats;

    log.kind     = OBSERVATION_RECORD_LOG;
    log.delivery = OBSERVATION_DELIVERY_RELIABLE;
    TEST_ASSERT_EQUAL_INT(SW_OK, observation_init(4U));
    TEST_ASSERT_EQUAL_INT(SW_OK, observation_publish(&log));
    TEST_ASSERT_EQUAL_INT(SW_OK, observation_publish(&log));
    TEST_ASSERT_EQUAL_INT(SW_OK, observation_try_pop(&out));
    TEST_ASSERT_EQUAL_UINT16(2U, out.repeat_count);
    TEST_ASSERT_NOT_NULL(strstr((const char *)out.payload, "重复=2"));
    TEST_ASSERT_EQUAL_INT(SW_ERR_NOT_FOUND, observation_try_pop(&out));
    TEST_ASSERT_EQUAL_INT(SW_OK, observation_get_stats(&stats));
    TEST_ASSERT_EQUAL_UINT64(1U, stats.reliable_coalesced_count);
    TEST_ASSERT_EQUAL_UINT64(2U, stats.published_count);
}

static void test_observation_rate_limits_best_effort_logs(void)
{
    observation_record_spec_t log = make_record(OBSERVATION_SEVERITY_INFO, 305U, "chatty");
    observation_stats_t       stats;
    uint32_t                  index;

    log.kind = OBSERVATION_RECORD_LOG;
    TEST_ASSERT_EQUAL_INT(SW_OK, observation_init(5U));
    for (index = 0U; index < OBSERVATION_LOG_RATE_LIMIT_PER_SEC; ++index) {
        TEST_ASSERT_EQUAL_INT(SW_OK, observation_publish(&log));
    }
    TEST_ASSERT_EQUAL_INT(SW_ERR_OVERFLOW, observation_publish(&log));
    TEST_ASSERT_EQUAL_INT(SW_OK, observation_get_stats(&stats));
    TEST_ASSERT_EQUAL_UINT64(1U, stats.dropped_rate_limited_count);
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
    /* 必须第一个跑：它断言"未初始化时不就绪"，依赖静态变量的进程初值 false。
     * observation 没有 reset 接口，一旦别的用例先 init 过就无法回到未初始化态。 */
    WDF_RUN_TEST(test_observation_ready_tracks_init, "", "验证观测系统就绪跟踪初始化");
    WDF_RUN_TEST(test_observation_rejects_invalid_boot_id, "", "验证观测系统拒绝无效启动ID");
    WDF_RUN_TEST(
        test_observation_stamps_context_and_prioritizes_critical, "", "验证观测系统写入上下文并优先处理严重级");
    WDF_RUN_TEST(test_observation_reports_normal_queue_overflow, "", "验证观测系统上报普通队列溢出");
    WDF_RUN_TEST(test_observation_offline_keeps_only_latest_and_reliable, "", "验证离线时仅保留最新值与可靠记录");
    WDF_RUN_TEST(test_observation_coalesces_repeated_reliable_logs, "", "验证可靠日志重复聚合");
    WDF_RUN_TEST(test_observation_rate_limits_best_effort_logs, "", "验证普通日志按秒限流");
    WDF_RUN_TEST(test_blackbox_preserves_configured_pre_and_post_window, "", "验证黑匣子保留配置的前置和后置窗口");
    WDF_RUN_TEST(test_blackbox_rejects_window_larger_than_capacity, "", "验证黑匣子拒绝超过容量的窗口配置");
    return UNITY_END();
}
