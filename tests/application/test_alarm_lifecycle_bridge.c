/**
 * @file    test_alarm_lifecycle_bridge.c
 * @brief   洗车会话生命周期桥接单元测试
 * @author  HUWANGWEI
 * @date    2026-08-03
 *
 * @note    该桥接此前零测试覆盖。它决定 session journal 何时开启与关闭，
 *          而 journal 决定洗完后哪些 MAJOR 报警会把设备推入 EXCEPTION——
 *          这条链路错了，现场表现是"洗完车该停机却没停"，属安全相关。
 */

#include "application/bridges/alarm_bridge.h"
#include "common/event_types.h"
#include "common/sw_error.h"
#include "common/time_util.h"
#include "domain/op_mode/op_mode_types.h"
#include "domain/safety/alarm_registry/alarm_registry.h"
#include "domain/safety/model/alarm_types.h"
#include "runtime/event_bus/event_bus.h"
#include "wdf_test_spec.h"

/* 会话日志只记 MAJOR 及以上（见 safety_matrix 的 records_in_journal），
 * 因此目录里同时放 MINOR 与 MAJOR，用于验证筛选而非只验证"有记录"。 */
enum {
    TEST_CODE_MAJOR    = 201105U,
    TEST_CODE_MAJOR_2  = 201205U,
    TEST_CODE_MINOR    = 901001U,
    TEST_CODE_CRITICAL = 201709U,
};

static const alarm_def_t s_catalog[] = {
    {
     .code         = TEST_CODE_MAJOR,
     .level        = ALARM_LEVEL_MAJOR,
     .clear        = ALARM_CLEAR_MANUAL_RESET,
     .reeval_group = ALARM_REEVAL_GROUP_NONE,
     .desc         = "major a",
     },
    {
     .code         = TEST_CODE_MAJOR_2,
     .level        = ALARM_LEVEL_MAJOR,
     .clear        = ALARM_CLEAR_MANUAL_RESET,
     .reeval_group = ALARM_REEVAL_GROUP_NONE,
     .desc         = "major b",
     },
    {
     .code         = TEST_CODE_MINOR,
     .level        = ALARM_LEVEL_MINOR,
     .clear        = ALARM_CLEAR_AUTO_STATIC,
     .reeval_group = ALARM_REEVAL_GROUP_NONE,
     .desc         = "minor",
     },
    {
     .code         = TEST_CODE_CRITICAL,
     .level        = ALARM_LEVEL_CRITICAL,
     .clear        = ALARM_CLEAR_MANUAL_RESET,
     .reeval_group = ALARM_REEVAL_GROUP_NONE,
     .desc         = "critical",
     },
};

/**
 * @brief  发布事件并同步排空，使桥接 handler 在本线程执行完毕
 */
static void publish_and_wait(event_type_t type, uint32_t param)
{
    TEST_ASSERT_EQUAL_INT(SW_OK, event_publish(type, param));
    TEST_ASSERT_EQUAL_INT(SW_OK, event_bus_drain());
}

static unsigned journal_count(void)
{
    uint32_t buf[ALARM_SESSION_JOURNAL_MAX];

    return alarm_registry_get_session_journal(buf, ALARM_SESSION_JOURNAL_MAX, NULL);
}

static uint32_t journal_dropped(void)
{
    uint32_t dropped = 0U;

    (void)alarm_registry_get_session_journal(NULL, 0U, &dropped);
    return dropped;
}

void setUp(void)
{
    time_util_init();
    TEST_ASSERT_EQUAL_INT(SW_OK, event_bus_init());
    TEST_ASSERT_EQUAL_INT(SW_OK, alarm_registry_init());
    TEST_ASSERT_EQUAL_INT(SW_OK, alarm_registry_load_catalog(s_catalog, sizeof(s_catalog) / sizeof(s_catalog[0])));
    TEST_ASSERT_EQUAL_INT(SW_OK, alarm_bridge_init());
}

void tearDown(void)
{
}

/* 会话未开始时触发的报警不进日志：否则上一轮遗留的报警会被算进本次洗车 */
static void test_no_journal_before_session_start(void)
{
    TEST_ASSERT_EQUAL_INT(SW_OK, alarm_registry_trigger(TEST_CODE_MAJOR));
    TEST_ASSERT_EQUAL_UINT(0U, journal_count());
}

/* SESSION_STARTED 后 MAJOR 报警进日志 */
static void test_session_started_enables_journal(void)
{
    publish_and_wait(EVT_WASH_SESSION_STARTED, 0U);

    TEST_ASSERT_EQUAL_INT(SW_OK, alarm_registry_trigger(TEST_CODE_MAJOR));
    TEST_ASSERT_EQUAL_UINT(1U, journal_count());
}

/* WASH_DONE 关闭日志：洗车结束后新触发的报警属于下一轮，不应计入本轮 */
static void test_wash_done_disables_journal(void)
{
    publish_and_wait(EVT_WASH_SESSION_STARTED, 0U);
    TEST_ASSERT_EQUAL_INT(SW_OK, alarm_registry_trigger(TEST_CODE_MAJOR));
    TEST_ASSERT_EQUAL_UINT(1U, journal_count());

    publish_and_wait(EVT_WASH_DONE, 0U);

    /* 已记录的不被清空（洗后评估要读它），但新报警不再追加 */
    TEST_ASSERT_EQUAL_INT(SW_OK, alarm_registry_trigger(TEST_CODE_MAJOR_2));
    TEST_ASSERT_EQUAL_UINT(1U, journal_count());
}

/* WASH_ABORTED 与 WASH_DONE 同样关闭日志：中止也是会话结束 */
static void test_wash_aborted_disables_journal(void)
{
    publish_and_wait(EVT_WASH_SESSION_STARTED, 0U);
    TEST_ASSERT_EQUAL_INT(SW_OK, alarm_registry_trigger(TEST_CODE_MAJOR));

    publish_and_wait(EVT_WASH_ABORTED, wash_abort_evt_param(WASH_ABORT_MANUAL));

    TEST_ASSERT_EQUAL_INT(SW_OK, alarm_registry_trigger(TEST_CODE_MAJOR_2));
    TEST_ASSERT_EQUAL_UINT(1U, journal_count());
}

/* 新会话开始时清空上一轮日志：否则上轮报警会让本轮洗完直接进 EXCEPTION */
static void test_new_session_clears_previous_journal(void)
{
    publish_and_wait(EVT_WASH_SESSION_STARTED, 0U);
    TEST_ASSERT_EQUAL_INT(SW_OK, alarm_registry_trigger(TEST_CODE_MAJOR));
    TEST_ASSERT_EQUAL_UINT(1U, journal_count());
    publish_and_wait(EVT_WASH_DONE, 0U);

    publish_and_wait(EVT_WASH_SESSION_STARTED, 0U);
    TEST_ASSERT_EQUAL_UINT(0U, journal_count());
}

/* MINOR 不进日志：它不阻塞开洗，也不该让洗完后转 EXCEPTION */
static void test_minor_not_recorded(void)
{
    publish_and_wait(EVT_WASH_SESSION_STARTED, 0U);

    TEST_ASSERT_EQUAL_INT(SW_OK, alarm_registry_trigger(TEST_CODE_MINOR));
    TEST_ASSERT_EQUAL_UINT(0U, journal_count());

    /* 同一会话内 MAJOR 仍能正常记录，确认上面不是"日志根本没开" */
    TEST_ASSERT_EQUAL_INT(SW_OK, alarm_registry_trigger(TEST_CODE_MAJOR));
    TEST_ASSERT_EQUAL_UINT(1U, journal_count());
}

/* CRITICAL 也进日志（records_in_journal 为真） */
static void test_critical_recorded(void)
{
    publish_and_wait(EVT_WASH_SESSION_STARTED, 0U);

    TEST_ASSERT_EQUAL_INT(SW_OK, alarm_registry_trigger(TEST_CODE_CRITICAL));
    TEST_ASSERT_EQUAL_UINT(1U, journal_count());
}

/* 同一报警重复触发只记一次，避免抖动刷满日志 */
static void test_duplicate_trigger_recorded_once(void)
{
    publish_and_wait(EVT_WASH_SESSION_STARTED, 0U);

    TEST_ASSERT_EQUAL_INT(SW_OK, alarm_registry_trigger(TEST_CODE_MAJOR));
    TEST_ASSERT_EQUAL_INT(SW_OK, alarm_registry_trigger(TEST_CODE_MAJOR));
    TEST_ASSERT_EQUAL_INT(SW_OK, alarm_registry_trigger(TEST_CODE_MAJOR));

    TEST_ASSERT_EQUAL_UINT(1U, journal_count());
}

/* 报警清除后日志保留：洗后评估读的是"本轮出现过什么"，不是"当前还有什么" */
static void test_journal_retains_cleared_alarm(void)
{
    publish_and_wait(EVT_WASH_SESSION_STARTED, 0U);

    TEST_ASSERT_EQUAL_INT(SW_OK, alarm_registry_trigger(TEST_CODE_MAJOR));
    TEST_ASSERT_EQUAL_INT(SW_OK, alarm_registry_clear(TEST_CODE_MAJOR));

    TEST_ASSERT_EQUAL_UINT(1U, journal_count());
}

/* 满池丢弃可观测、读取不清零、MINOR 不增加丢弃、新会话清零 */
static void test_journal_overflow_dropped_survives_read_until_new_session(void)
{
    alarm_def_t cat[ALARM_SESSION_JOURNAL_MAX + 2U];
    unsigned    i;
    uint32_t    extra;
    uint32_t    minor_code = TEST_CODE_MINOR;

    for (i = 0U; i < (ALARM_SESSION_JOURNAL_MAX + 1U); ++i) {
        cat[i] = (alarm_def_t){
            .code         = ALARM_CODE_MAKE(ALM_C_SENSE, i, ALM_N_OVERLOAD),
            .level        = ALARM_LEVEL_MAJOR,
            .clear        = ALARM_CLEAR_AUTO_STATIC,
            .reeval_group = ALARM_REEVAL_GROUP_NONE,
            .desc         = "journal fill",
        };
    }
    extra                               = ALARM_CODE_MAKE(ALM_C_SENSE, ALARM_SESSION_JOURNAL_MAX, ALM_N_OVERLOAD);
    cat[ALARM_SESSION_JOURNAL_MAX + 1U] = (alarm_def_t){
        .code         = minor_code,
        .level        = ALARM_LEVEL_MINOR,
        .clear        = ALARM_CLEAR_AUTO_STATIC,
        .reeval_group = ALARM_REEVAL_GROUP_NONE,
        .desc         = "minor",
    };

    TEST_ASSERT_EQUAL_INT(SW_OK, alarm_registry_load_catalog(cat, ALARM_SESSION_JOURNAL_MAX + 2U));
    publish_and_wait(EVT_WASH_SESSION_STARTED, 0U);

    for (i = 0U; i < ALARM_SESSION_JOURNAL_MAX; ++i) {
        TEST_ASSERT_EQUAL_INT(SW_OK, alarm_registry_trigger(ALARM_CODE_MAKE(ALM_C_SENSE, i, ALM_N_OVERLOAD)));
    }
    TEST_ASSERT_EQUAL_UINT(ALARM_SESSION_JOURNAL_MAX, journal_count());
    TEST_ASSERT_EQUAL_UINT32(0U, journal_dropped());

    TEST_ASSERT_EQUAL_INT(SW_OK, alarm_registry_trigger(extra));
    TEST_ASSERT_EQUAL_UINT32(1U, journal_dropped());
    TEST_ASSERT_EQUAL_UINT32(1U, journal_dropped());

    TEST_ASSERT_EQUAL_INT(SW_OK, alarm_registry_trigger(minor_code));
    TEST_ASSERT_EQUAL_UINT32(1U, journal_dropped());

    publish_and_wait(EVT_WASH_SESSION_STARTED, 0U);
    TEST_ASSERT_EQUAL_UINT(0U, journal_count());
    TEST_ASSERT_EQUAL_UINT32(0U, journal_dropped());
}

int main(void)
{
    UNITY_BEGIN();
    WDF_RUN_TEST(test_no_journal_before_session_start, "", "验证会话启动前不记录报警日志");
    WDF_RUN_TEST(test_session_started_enables_journal, "", "验证会话已启动启用会话日志");
    WDF_RUN_TEST(test_wash_done_disables_journal, "", "验证洗车完成禁用会话日志");
    WDF_RUN_TEST(test_wash_aborted_disables_journal, "", "验证洗车已中止禁用会话日志");
    WDF_RUN_TEST(test_new_session_clears_previous_journal, "", "验证新会话清除原有会话日志");
    WDF_RUN_TEST(test_minor_not_recorded, "", "验证轻微级未被记录");
    WDF_RUN_TEST(test_critical_recorded, "", "验证严重级被记录");
    WDF_RUN_TEST(test_duplicate_trigger_recorded_once, "", "验证重复触发源被记录一次");
    WDF_RUN_TEST(test_journal_retains_cleared_alarm, "", "验证会话日志保留已清除报警");
    WDF_RUN_TEST(test_journal_overflow_dropped_survives_read_until_new_session,
                 "ALRM-21",
                 "验证会话日志满池丢弃可观测且新会话清零");
    return UNITY_END();
}
