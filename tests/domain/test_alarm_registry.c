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
     .code         = 201101U,
     .level        = ALARM_LEVEL_MAJOR,
     .clear        = ALARM_CLEAR_MANUAL_RESET,
     .reeval_group = ALARM_REEVAL_GROUP_NONE,
     .desc         = "侧刷过载",
     },
    {
     .code         = 201709U,
     .level        = ALARM_LEVEL_CRITICAL,
     .clear        = ALARM_CLEAR_AUTO_STATIC,
     .reeval_group = ALARM_REEVAL_GROUP_NONE,
     .desc         = "急停",
     },
    {
     .code         = 200205U,
     .level        = ALARM_LEVEL_MAJOR,
     .clear        = ALARM_CLEAR_ON_MOTION,
     .reeval_group = (motion_reeval_group_id_t)1U,
     .desc         = "龙门前限位超时",
     },
};

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
    alarm_registry_init();
    (void)alarm_registry_load_catalog(s_catalog, 3U);
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
    TEST_ASSERT_TRUE(alarm_registry_has_blocking_active());
}

static void test_reset_requires_manual_condition_clear(void)
{
    (void)alarm_registry_trigger(201709U);
    (void)alarm_registry_trigger(201101U);
    alarm_registry_reset_all();
    TEST_ASSERT_TRUE(alarm_registry_is_active(201709U));
    TEST_ASSERT_TRUE(alarm_registry_is_active(201101U));

    (void)alarm_registry_clear(201101U);
    TEST_ASSERT_TRUE(alarm_registry_is_active(201101U));
    alarm_registry_reset_all();
    TEST_ASSERT_FALSE(alarm_registry_is_active(201101U));
}

static void test_manual_condition_clear_waits_for_reset(void)
{
    (void)alarm_registry_trigger(201101U);
    (void)alarm_registry_clear(201101U);
    TEST_ASSERT_TRUE(alarm_registry_is_active(201101U));

    alarm_registry_reset_all();
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

static void test_active_pool_full_rejects(void)
{
    alarm_def_t cat[ALARM_ACTIVE_MAX + 1U];
    unsigned    i;

    for (i = 0U; i < (ALARM_ACTIVE_MAX + 1U); ++i) {
        cat[i] = (alarm_def_t){
            .code         = 902100U + i,
            .level        = ALARM_LEVEL_MINOR,
            .clear        = ALARM_CLEAR_AUTO_STATIC,
            .reeval_group = ALARM_REEVAL_GROUP_NONE,
            .desc         = "minor fill",
        };
    }

    (void)alarm_registry_load_catalog(cat, ALARM_ACTIVE_MAX + 1U);
    for (i = 0U; i < ALARM_ACTIVE_MAX; ++i) {
        TEST_ASSERT_EQUAL_INT(SW_OK, alarm_registry_trigger(902100U + i));
    }
    TEST_ASSERT_EQUAL_INT(SW_ERR_OVERFLOW, alarm_registry_trigger(902100U + ALARM_ACTIVE_MAX));
}

/* 合并读接口一次返回四项，且与分别调用的旧接口结果一致 */
static void test_copy_safety_view_returns_consistent_snapshot(void)
{
    alarm_instance_t list[ALARM_ACTIVE_MAX];
    bool             blocking = false;
    uint32_t         top      = ALARM_CODE_NONE;
    safety_posture_t posture  = SAFETY_POSTURE_NOMINAL;
    unsigned         n;

    (void)alarm_registry_trigger(201101U); /* MAJOR */
    (void)alarm_registry_trigger(201709U); /* CRITICAL */

    n = alarm_registry_copy_safety_view(list, ALARM_ACTIVE_MAX, &blocking, &top, &posture);

    TEST_ASSERT_EQUAL_UINT(2U, n);
    TEST_ASSERT_TRUE(blocking);
    TEST_ASSERT_EQUAL_UINT(201709U, top); /* CRITICAL 级别最高 */
    TEST_ASSERT_EQUAL_INT(SAFETY_POSTURE_LOCKOUT, posture);

    /* 与旧接口逐项对齐，确认重构未改变语义 */
    {
        alarm_instance_t old_list[ALARM_ACTIVE_MAX];
        bool             old_blocking = false;
        uint32_t         old_top      = ALARM_CODE_NONE;
        unsigned         old_n;

        old_n = alarm_registry_copy_active_projection(old_list, ALARM_ACTIVE_MAX, &old_blocking, &old_top);
        TEST_ASSERT_EQUAL_UINT(old_n, n);
        TEST_ASSERT_EQUAL_INT(old_blocking, blocking);
        TEST_ASSERT_EQUAL_UINT(old_top, top);
        TEST_ASSERT_EQUAL_INT(alarm_registry_safety_posture(), posture);
    }
}

/* 空表与 NULL 出参均不得崩溃 */
static void test_copy_safety_view_handles_empty_and_null(void)
{
    safety_posture_t posture = SAFETY_POSTURE_LOCKOUT;
    unsigned         n;

    n = alarm_registry_copy_safety_view(NULL, 0U, NULL, NULL, &posture);
    TEST_ASSERT_EQUAL_UINT(0U, n);
    TEST_ASSERT_EQUAL_INT(SAFETY_POSTURE_NOMINAL, posture);

    (void)alarm_registry_trigger(201101U);
    n = alarm_registry_copy_safety_view(NULL, 0U, NULL, NULL, NULL);
    TEST_ASSERT_EQUAL_UINT(1U, n);
}

/* list_max 小于活动数时按容量截断，但聚合值仍反映全部活动告警 */
static void test_copy_safety_view_truncates_list_but_not_aggregates(void)
{
    alarm_instance_t one[1];
    bool             blocking = false;
    uint32_t         top      = ALARM_CODE_NONE;
    safety_posture_t posture  = SAFETY_POSTURE_NOMINAL;
    unsigned         n;

    (void)alarm_registry_trigger(201101U);
    (void)alarm_registry_trigger(201709U);

    n = alarm_registry_copy_safety_view(one, 1U, &blocking, &top, &posture);

    TEST_ASSERT_EQUAL_UINT(1U, n); /* 只装得下一条 */
    TEST_ASSERT_TRUE(blocking);
    TEST_ASSERT_EQUAL_UINT(201709U, top);
    TEST_ASSERT_EQUAL_INT(SAFETY_POSTURE_LOCKOUT, posture);
}

/* 未知告警码在锁内查表后仍返回 PARAM，不改变活动表 */
static void test_unknown_code_rejected(void)
{
    TEST_ASSERT_EQUAL_INT(SW_ERR_PARAM, alarm_registry_trigger(999999U));
    TEST_ASSERT_EQUAL_INT(SW_ERR_PARAM, alarm_registry_clear(999999U));
    TEST_ASSERT_FALSE(alarm_registry_is_active(999999U));
}

int main(void)
{
    UNITY_BEGIN();

    RUN_TEST(test_alarm_code_helpers_make_decode_and_validate);
    RUN_TEST(test_trigger_clear_idempotent);
    RUN_TEST(test_major_not_lockout);
    RUN_TEST(test_critical_lockout);
    RUN_TEST(test_reset_requires_manual_condition_clear);
    RUN_TEST(test_manual_condition_clear_waits_for_reset);
    RUN_TEST(test_reevaluate_by_group);
    RUN_TEST(test_pull_events);
    RUN_TEST(test_session_journal_blocking_levels);
    RUN_TEST(test_active_pool_full_rejects);
    RUN_TEST(test_copy_safety_view_returns_consistent_snapshot);
    RUN_TEST(test_copy_safety_view_handles_empty_and_null);
    RUN_TEST(test_copy_safety_view_truncates_list_but_not_aggregates);
    RUN_TEST(test_unknown_code_rejected);

    return UNITY_END();
}
