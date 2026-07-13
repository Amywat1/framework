/**
 * @file    test_alarm_registry.c
 * @brief   alarm_registry 单元测试
 */

#include "common/sw_error.h"
#include "domain/safety/alarm_registry/alarm_registry.h"
#include "domain/safety/model/alarm_types.h"
#include "unity.h"

static const alarm_def_t s_catalog[] = {
    {
     .code             = 201101U,
     .level            = ALARM_LEVEL_MAJOR,
     .response         = RESP_COMPLETE_THEN_ASSESS,
     .clear            = ALARM_CLEAR_MANUAL_RESET,
     .source_kind      = ALARM_SOURCE_LEVEL,
     .reeval_group     = ALARM_REEVAL_GROUP_NONE,
     .immediate_cutout = false,
     .desc             = "侧刷过载",
     },
    {
     .code             = 201709U,
     .level            = ALARM_LEVEL_CRITICAL,
     .response         = RESP_STOP_IMMEDIATELY,
     .clear            = ALARM_CLEAR_AUTO_STATIC,
     .source_kind      = ALARM_SOURCE_LEVEL,
     .reeval_group     = ALARM_REEVAL_GROUP_NONE,
     .immediate_cutout = false,
     .desc             = "急停",
     },
    {
     .code             = 200205U,
     .level            = ALARM_LEVEL_MAJOR,
     .response         = RESP_COMPLETE_THEN_ASSESS,
     .clear            = ALARM_CLEAR_ON_MOTION,
     .source_kind      = ALARM_SOURCE_PROCESS,
     .reeval_group     = (motion_reeval_group_id_t)1U,
     .immediate_cutout = false,
     .desc             = "龙门前限位超时",
     },
};

static bool skip_estop(uint32_t code)
{
    return code == 201709U;
}

static void test_alarm_code_helpers_make_decode_and_validate(void)
{
    uint32_t code = 0U;

    TEST_ASSERT_EQUAL_UINT(201101U, ALARM_CODE_MAKE(ALM_C_SENSE, 11U, ALM_N_OVERLOAD));
    TEST_ASSERT_EQUAL_UINT(ALM_C_SENSE, ALARM_CODE_CATEGORY(201101U));
    TEST_ASSERT_EQUAL_UINT(11U, ALARM_CODE_INDEX(201101U));
    TEST_ASSERT_EQUAL_UINT(ALM_N_OVERLOAD, ALARM_CODE_NATURE(201101U));
    TEST_ASSERT_TRUE(alarm_code_is_valid(201101U));
    TEST_ASSERT_FALSE(alarm_code_is_valid(ALARM_CODE_NONE));
    TEST_ASSERT_FALSE(alarm_code_is_valid(1000000U));

    TEST_ASSERT_TRUE(alarm_code_make_checked(ALM_C_CTRL, 3U, ALM_N_HW_FAULT, &code));
    TEST_ASSERT_EQUAL_UINT(400303U, code);
    TEST_ASSERT_FALSE(alarm_code_make_checked(0U, 3U, ALM_N_HW_FAULT, &code));
    TEST_ASSERT_FALSE(alarm_code_make_checked(ALM_C_CTRL, 1000U, ALM_N_HW_FAULT, &code));
    TEST_ASSERT_FALSE(alarm_code_make_checked(ALM_C_CTRL, 3U, 100U, &code));
    TEST_ASSERT_FALSE(alarm_code_make_checked(ALM_C_CTRL, 3U, ALM_N_HW_FAULT, NULL));
}

void setUp(void)
{
    (void)alarm_registry_init();
    (void)alarm_registry_load_catalog(s_catalog, 3U);
    alarm_registry_set_estop_skip_fn(skip_estop);
}

void tearDown(void)
{
}

static void test_trigger_clear_idempotent(void)
{
    TEST_ASSERT_EQUAL_INT(SW_OK, alarm_registry_trigger(201101U));
    TEST_ASSERT_TRUE(alarm_registry_is_active(201101U));
    TEST_ASSERT_EQUAL_INT(SW_OK, alarm_registry_trigger(201101U));
    TEST_ASSERT_EQUAL_INT(SW_OK, alarm_registry_clear(201101U));
    TEST_ASSERT_TRUE(alarm_registry_is_active(201101U));
}

static void test_major_not_lockout(void)
{
    (void)alarm_registry_trigger(201101U);
    TEST_ASSERT_EQUAL_INT(SAFETY_POSTURE_NOMINAL, alarm_registry_safety_posture());
    TEST_ASSERT_TRUE(alarm_registry_has_blocking_active());
}

static void test_critical_lockout(void)
{
    (void)alarm_registry_trigger(201709U);
    TEST_ASSERT_EQUAL_INT(SAFETY_POSTURE_LOCKOUT, alarm_registry_safety_posture());
    TEST_ASSERT_TRUE(safety_is_warning_active());
}

static void test_warning_active_major_only(void)
{
    (void)alarm_registry_trigger(201101U);
    TEST_ASSERT_TRUE(safety_is_warning_active());
}

static void test_recover_skips_estop_clears_manual(void)
{
    (void)alarm_registry_trigger(201709U);
    (void)alarm_registry_trigger(201101U);
    (void)alarm_registry_recover_all();
    TEST_ASSERT_TRUE(alarm_registry_is_active(201709U));
    TEST_ASSERT_FALSE(alarm_registry_is_active(201101U));
}

static void test_reevaluate_by_group(void)
{
    (void)alarm_registry_trigger(200205U);
    TEST_ASSERT_TRUE(alarm_registry_is_active(200205U));
    (void)alarm_registry_reevaluate_group((motion_reeval_group_id_t)1U);
    TEST_ASSERT_FALSE(alarm_registry_is_active(200205U));
}

static void test_pull_events(void)
{
    alarm_domain_event_t ev[4];
    unsigned             n;

    (void)alarm_registry_trigger(201101U);
    n = alarm_registry_pull_events(ev, 4U);
    TEST_ASSERT_EQUAL_UINT(1U, n);
    TEST_ASSERT_EQUAL_INT(ALARM_DOMAIN_EVT_TRIGGERED, (int)ev[0].kind);
    TEST_ASSERT_EQUAL_UINT(201101U, ev[0].code);
}

static void test_session_journal_blocking_levels(void)
{
    uint32_t codes[4];
    unsigned n;

    alarm_registry_on_wash_session_started();
    (void)alarm_registry_trigger(201101U);
    (void)alarm_registry_trigger(201709U);
    n = alarm_registry_get_session_journal(codes, 4U);
    TEST_ASSERT_EQUAL_UINT(2U, n);
    TEST_ASSERT_EQUAL_UINT(201101U, codes[0U]);
    TEST_ASSERT_EQUAL_UINT(201709U, codes[1U]);
}

static void test_overflow_meta_no_crash(void)
{
    alarm_def_t cat[33U];
    unsigned    i;

    for (i = 0U; i < 33U; ++i) {
        cat[i] = (alarm_def_t){
            .code             = 902100U + i,
            .level            = ALARM_LEVEL_MINOR,
            .response         = RESP_LOG_ONLY,
            .clear            = ALARM_CLEAR_AUTO_STATIC,
            .source_kind      = ALARM_SOURCE_LEVEL,
            .reeval_group     = ALARM_REEVAL_GROUP_NONE,
            .immediate_cutout = false,
            .desc             = "minor fill",
        };
    }

    (void)alarm_registry_load_catalog(cat, 33U);
    for (i = 0U; i < 32U; ++i) {
        TEST_ASSERT_EQUAL_INT(SW_OK, alarm_registry_trigger(902100U + i));
    }
    TEST_ASSERT_EQUAL_INT(SW_ERR_OVERFLOW, alarm_registry_trigger(902132U));
    TEST_ASSERT_TRUE(alarm_registry_is_active(ALARM_CODE_ACTIVE_POOL_OVERFLOW));
}

int main(void)
{
    UNITY_BEGIN();

    RUN_TEST(test_alarm_code_helpers_make_decode_and_validate);
    RUN_TEST(test_trigger_clear_idempotent);
    RUN_TEST(test_major_not_lockout);
    RUN_TEST(test_critical_lockout);
    RUN_TEST(test_warning_active_major_only);
    RUN_TEST(test_recover_skips_estop_clears_manual);
    RUN_TEST(test_reevaluate_by_group);
    RUN_TEST(test_pull_events);
    RUN_TEST(test_session_journal_blocking_levels);
    RUN_TEST(test_overflow_meta_no_crash);

    return UNITY_END();
}
