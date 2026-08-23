/**
 * @file    test_alarm_registry.c
 * @brief   alarm_registry 单元测试
 */

#include "common/sw_error.h"
#include "domain/safety/alarm_registry/alarm_registry.h"
#include "domain/safety/model/alarm_types.h"
#include "wdf_test_spec.h"

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

    /* 条件仍成立：运动结束重评估不得清警 */
    (void)alarm_registry_reevaluate_group((motion_reeval_group_id_t)1U);
    TEST_ASSERT_TRUE(alarm_registry_is_active(200205U));

    /* 动作中判定正常后，再重评估才落地清除 */
    (void)alarm_registry_clear(200205U);
    TEST_ASSERT_TRUE(alarm_registry_is_active(200205U));
    (void)alarm_registry_reevaluate_group((motion_reeval_group_id_t)1U);
    TEST_ASSERT_FALSE(alarm_registry_is_active(200205U));
}

static void test_pull_events(void)
{
    alarm_domain_event_t ev[4];
    uint32_t             dropped = 1U;
    unsigned             n;

    (void)alarm_registry_trigger(201101U);
    n = alarm_registry_pull_events(ev, 4U, &dropped);
    TEST_ASSERT_EQUAL_UINT(1U, n);
    TEST_ASSERT_EQUAL_UINT32(0U, dropped);
    TEST_ASSERT_EQUAL_INT(ALARM_DOMAIN_EVT_TRIGGERED, (int)ev[0].kind);
    TEST_ASSERT_EQUAL_UINT(201101U, ev[0].code);
}

/* 待发队列溢出必须被计数交出，且计数取走一次即清零——静默丢事件会让
 * 靠 EVT_ALARM_* 边沿维护派生状态的订阅者永远停在旧值 */
static void test_pending_overflow_is_reported_then_cleared(void)
{
    alarm_domain_event_t ev[ALARM_PENDING_EVENT_MAX];
    uint32_t             dropped  = 0U;
    unsigned             cycles   = (ALARM_PENDING_EVENT_MAX / 2U) + 3U;
    unsigned             expected = (2U * cycles) - ALARM_PENDING_EVENT_MAX;
    unsigned             i;
    unsigned             n;

    /* 201709 是 AUTO_STATIC：一次 trigger + clear 产生两条域事件而活动表始终 ≤1，
     * 因此能在不触碰活跃池上限的前提下把待发队列灌溢出。 */
    for (i = 0U; i < cycles; ++i) {
        TEST_ASSERT_EQUAL_INT(SW_OK, alarm_registry_trigger(201709U));
        TEST_ASSERT_EQUAL_INT(SW_OK, alarm_registry_clear(201709U));
    }

    n = alarm_registry_pull_events(ev, ALARM_PENDING_EVENT_MAX, &dropped);
    TEST_ASSERT_EQUAL_UINT(ALARM_PENDING_EVENT_MAX, n);
    TEST_ASSERT_EQUAL_UINT32(expected, dropped);

    /* 计数已随上一次取出清零，不会在下一拍被重复上报成新的丢弃 */
    dropped = 0xFFFFFFFFU;
    n       = alarm_registry_pull_events(ev, ALARM_PENDING_EVENT_MAX, &dropped);
    TEST_ASSERT_EQUAL_UINT(0U, n);
    TEST_ASSERT_EQUAL_UINT32(0U, dropped);
}

/* 目录内容非法时整表拒绝：这些错误在运行期不会报错，只会表现为
 * 「报警行为和配置对不上」，必须挡在装载期 */
static void test_load_catalog_rejects_invalid_defs(void)
{
    alarm_def_t bad[2];

    /* 非法码：大类为 0，不符合 6 位编码约定 */
    bad[0] = (alarm_def_t){
        .code         = 1234U,
        .level        = ALARM_LEVEL_MAJOR,
        .clear        = ALARM_CLEAR_MANUAL_RESET,
        .reeval_group = ALARM_REEVAL_GROUP_NONE,
        .desc         = "bad code",
    };
    TEST_ASSERT_EQUAL_INT(SW_ERR_PARAM, alarm_registry_load_catalog(bad, 1U));

    /* 重复码：后一条的等级与清除策略会被 find_def_index 静默忽略 */
    bad[0] = (alarm_def_t){
        .code         = 201101U,
        .level        = ALARM_LEVEL_MINOR,
        .clear        = ALARM_CLEAR_MANUAL_RESET,
        .reeval_group = ALARM_REEVAL_GROUP_NONE,
        .desc         = "first",
    };
    bad[1]       = bad[0];
    bad[1].level = ALARM_LEVEL_CRITICAL;
    TEST_ASSERT_EQUAL_INT(SW_ERR_PARAM, alarm_registry_load_catalog(bad, 2U));

    /* 未定义等级：不得落进行为矩阵的最严格兜底分支 */
    bad[0]       = bad[1];
    bad[0].level = (alarm_level_t)7;
    TEST_ASSERT_EQUAL_INT(SW_ERR_PARAM, alarm_registry_load_catalog(bad, 1U));

    /* ON_MOTION 缺分组：永远进不了任何重评估批次 */
    bad[0] = (alarm_def_t){
        .code         = 201101U,
        .level        = ALARM_LEVEL_MAJOR,
        .clear        = ALARM_CLEAR_ON_MOTION,
        .reeval_group = ALARM_REEVAL_GROUP_NONE,
        .desc         = "on motion without group",
    };
    TEST_ASSERT_EQUAL_INT(SW_ERR_PARAM, alarm_registry_load_catalog(bad, 1U));

    /* 非 ON_MOTION 却填了分组：该分组永远不会被重评估用到 */
    bad[0].clear        = ALARM_CLEAR_MANUAL_RESET;
    bad[0].reeval_group = (motion_reeval_group_id_t)3U;
    TEST_ASSERT_EQUAL_INT(SW_ERR_PARAM, alarm_registry_load_catalog(bad, 1U));

    /* 整表拒绝，不做部分装载：setUp 装的目录必须原封不动 */
    TEST_ASSERT_EQUAL_UINT(3U, alarm_registry_catalog_count());
    TEST_ASSERT_EQUAL_INT(SW_OK, alarm_registry_trigger(201101U));
}

/* 换目录必须连会话日志一起清：旧日志里的码在新目录中可能已不存在 */
static void test_load_catalog_resets_session_journal(void)
{
    uint32_t codes[4];

    alarm_registry_on_wash_session_started();
    (void)alarm_registry_trigger(201101U);
    TEST_ASSERT_EQUAL_UINT(1U, alarm_registry_get_session_journal(codes, 4U, NULL));

    TEST_ASSERT_EQUAL_INT(SW_OK, alarm_registry_load_catalog(s_catalog, 3U));
    TEST_ASSERT_EQUAL_UINT(0U, alarm_registry_get_session_journal(codes, 4U, NULL));

    /* 会话窗口同样关闭：重新开会话前触发不再记日志 */
    (void)alarm_registry_trigger(201101U);
    TEST_ASSERT_EQUAL_UINT(0U, alarm_registry_get_session_journal(codes, 4U, NULL));
}

static void test_session_journal_blocking_levels(void)
{
    uint32_t codes[4];
    unsigned n;

    alarm_registry_on_wash_session_started();
    (void)alarm_registry_trigger(201101U);
    (void)alarm_registry_trigger(201709U);
    n = alarm_registry_get_session_journal(codes, 4U, NULL);
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

static void discard_pending(void)
{
    alarm_domain_event_t ev[ALARM_PENDING_EVENT_MAX];
    uint32_t             dropped = 0U;

    (void)alarm_registry_pull_events(ev, ALARM_PENDING_EVENT_MAX, &dropped);
}

static alarm_def_t make_fill_def(uint32_t code, alarm_level_t level)
{
    return (alarm_def_t){
        .code         = code,
        .level        = level,
        .clear        = ALARM_CLEAR_AUTO_STATIC,
        .reeval_group = ALARM_REEVAL_GROUP_NONE,
        .desc         = "fill",
    };
}

static uint32_t fill_code(unsigned i)
{
    uint32_t code = 0U;

    TEST_ASSERT_TRUE(alarm_code_make_checked(ALM_C_SW, i, ALM_N_OTHER, &code));
    return code;
}

static void test_lockout_evicts_oldest_minor_when_pool_full(void)
{
    alarm_def_t          cat[ALARM_ACTIVE_MAX + 1U];
    alarm_domain_event_t ev[4];
    uint32_t             dropped = 0U;
    uint32_t             crit    = 201709U;
    unsigned             i;
    unsigned             n;

    for (i = 0U; i < ALARM_ACTIVE_MAX; ++i) {
        cat[i] = make_fill_def(fill_code(i), ALARM_LEVEL_MINOR);
    }
    cat[ALARM_ACTIVE_MAX] = make_fill_def(crit, ALARM_LEVEL_CRITICAL);

    TEST_ASSERT_EQUAL_INT(SW_OK, alarm_registry_load_catalog(cat, ALARM_ACTIVE_MAX + 1U));
    for (i = 0U; i < ALARM_ACTIVE_MAX; ++i) {
        TEST_ASSERT_EQUAL_INT(SW_OK, alarm_registry_trigger(fill_code(i)));
    }
    discard_pending();

    TEST_ASSERT_EQUAL_INT(SW_OK, alarm_registry_trigger(crit));
    TEST_ASSERT_TRUE(alarm_registry_is_active(crit));
    TEST_ASSERT_FALSE(alarm_registry_is_active(fill_code(0U)));
    TEST_ASSERT_TRUE(alarm_registry_is_active(fill_code(1U)));
    TEST_ASSERT_EQUAL_INT(SAFETY_POSTURE_LOCKOUT, alarm_registry_safety_posture());

    n = alarm_registry_pull_events(ev, 4U, &dropped);
    TEST_ASSERT_EQUAL_UINT(2U, n);
    TEST_ASSERT_EQUAL_UINT32(0U, dropped);
    TEST_ASSERT_EQUAL_INT(ALARM_DOMAIN_EVT_CLEARED, (int)ev[0].kind);
    TEST_ASSERT_EQUAL_UINT(fill_code(0U), ev[0].code);
    TEST_ASSERT_EQUAL_INT(ALARM_DOMAIN_EVT_TRIGGERED, (int)ev[1].kind);
    TEST_ASSERT_EQUAL_UINT(crit, ev[1].code);
}

static void test_lockout_evicts_oldest_major_when_no_minor(void)
{
    alarm_def_t cat[ALARM_ACTIVE_MAX + 1U];
    uint32_t    crit = 201709U;
    unsigned    i;

    for (i = 0U; i < ALARM_ACTIVE_MAX; ++i) {
        cat[i] = make_fill_def(fill_code(i), ALARM_LEVEL_MAJOR);
    }
    cat[ALARM_ACTIVE_MAX] = make_fill_def(crit, ALARM_LEVEL_CRITICAL);

    TEST_ASSERT_EQUAL_INT(SW_OK, alarm_registry_load_catalog(cat, ALARM_ACTIVE_MAX + 1U));
    for (i = 0U; i < ALARM_ACTIVE_MAX; ++i) {
        TEST_ASSERT_EQUAL_INT(SW_OK, alarm_registry_trigger(fill_code(i)));
    }
    discard_pending();

    TEST_ASSERT_EQUAL_INT(SW_OK, alarm_registry_trigger(crit));
    TEST_ASSERT_TRUE(alarm_registry_is_active(crit));
    TEST_ASSERT_FALSE(alarm_registry_is_active(fill_code(0U)));
}

static void test_lockout_rejected_when_pool_full_of_lockout(void)
{
    alarm_def_t          cat[ALARM_ACTIVE_MAX + 1U];
    alarm_domain_event_t ev[4];
    uint32_t             dropped = 0U;
    unsigned             i;
    unsigned             n;

    for (i = 0U; i < (ALARM_ACTIVE_MAX + 1U); ++i) {
        cat[i] = make_fill_def(fill_code(i), ALARM_LEVEL_CRITICAL);
    }

    TEST_ASSERT_EQUAL_INT(SW_OK, alarm_registry_load_catalog(cat, ALARM_ACTIVE_MAX + 1U));
    for (i = 0U; i < ALARM_ACTIVE_MAX; ++i) {
        TEST_ASSERT_EQUAL_INT(SW_OK, alarm_registry_trigger(fill_code(i)));
    }
    discard_pending();

    TEST_ASSERT_EQUAL_INT(SW_ERR_OVERFLOW, alarm_registry_trigger(fill_code(ALARM_ACTIVE_MAX)));
    TEST_ASSERT_FALSE(alarm_registry_is_active(fill_code(ALARM_ACTIVE_MAX)));
    TEST_ASSERT_TRUE(alarm_registry_is_active(fill_code(0U)));

    n = alarm_registry_pull_events(ev, 4U, &dropped);
    TEST_ASSERT_EQUAL_UINT(0U, n);
}

static void test_retrigger_active_lockout_does_not_evict(void)
{
    alarm_def_t          cat[ALARM_ACTIVE_MAX];
    alarm_domain_event_t ev[4];
    uint32_t             dropped = 0U;
    uint32_t             crit    = fill_code(ALARM_ACTIVE_MAX - 1U);
    unsigned             i;
    unsigned             n;

    for (i = 0U; i < (ALARM_ACTIVE_MAX - 1U); ++i) {
        cat[i] = make_fill_def(fill_code(i), ALARM_LEVEL_MINOR);
    }
    cat[ALARM_ACTIVE_MAX - 1U] = make_fill_def(crit, ALARM_LEVEL_CRITICAL);

    TEST_ASSERT_EQUAL_INT(SW_OK, alarm_registry_load_catalog(cat, ALARM_ACTIVE_MAX));
    for (i = 0U; i < ALARM_ACTIVE_MAX; ++i) {
        TEST_ASSERT_EQUAL_INT(SW_OK, alarm_registry_trigger(fill_code(i)));
    }
    discard_pending();

    TEST_ASSERT_EQUAL_INT(SW_OK, alarm_registry_trigger(crit));
    TEST_ASSERT_TRUE(alarm_registry_is_active(fill_code(0U)));
    TEST_ASSERT_TRUE(alarm_registry_is_active(crit));

    n = alarm_registry_pull_events(ev, 4U, &dropped);
    TEST_ASSERT_EQUAL_UINT(0U, n);
}

static void test_session_journal_overflow_is_counted_and_not_cleared_on_read(void)
{
    alarm_def_t cat[ALARM_SESSION_JOURNAL_MAX + 2U];
    uint32_t    codes[ALARM_SESSION_JOURNAL_MAX];
    uint32_t    dropped = 0U;
    uint32_t    extra;
    uint32_t    minor;
    unsigned    i;
    unsigned    n;

    for (i = 0U; i < (ALARM_SESSION_JOURNAL_MAX + 1U); ++i) {
        cat[i] = make_fill_def(fill_code(i), ALARM_LEVEL_MAJOR);
    }
    extra              = fill_code(ALARM_SESSION_JOURNAL_MAX);
    minor              = ALARM_CODE_MAKE(ALM_C_SW, 50U, ALM_N_OTHER);
    cat[ALARM_SESSION_JOURNAL_MAX + 1U] = make_fill_def(minor, ALARM_LEVEL_MINOR);

    TEST_ASSERT_EQUAL_INT(SW_OK, alarm_registry_load_catalog(cat, ALARM_SESSION_JOURNAL_MAX + 2U));
    alarm_registry_on_wash_session_started();

    TEST_ASSERT_EQUAL_INT(SW_OK, alarm_registry_trigger(fill_code(0U)));
    TEST_ASSERT_EQUAL_INT(SW_OK, alarm_registry_trigger(fill_code(0U)));
    n = alarm_registry_get_session_journal(codes, ALARM_SESSION_JOURNAL_MAX, &dropped);
    TEST_ASSERT_EQUAL_UINT(1U, n);
    TEST_ASSERT_EQUAL_UINT32(0U, dropped);

    for (i = 1U; i < ALARM_SESSION_JOURNAL_MAX; ++i) {
        TEST_ASSERT_EQUAL_INT(SW_OK, alarm_registry_trigger(fill_code(i)));
    }
    TEST_ASSERT_EQUAL_INT(SW_OK, alarm_registry_trigger(extra));
    TEST_ASSERT_TRUE(alarm_registry_is_active(extra));

    n = alarm_registry_get_session_journal(codes, ALARM_SESSION_JOURNAL_MAX, &dropped);
    TEST_ASSERT_EQUAL_UINT(ALARM_SESSION_JOURNAL_MAX, n);
    TEST_ASSERT_EQUAL_UINT32(1U, dropped);

    TEST_ASSERT_EQUAL_INT(SW_OK, alarm_registry_trigger(minor));
    n = alarm_registry_get_session_journal(codes, ALARM_SESSION_JOURNAL_MAX, &dropped);
    TEST_ASSERT_EQUAL_UINT(ALARM_SESSION_JOURNAL_MAX, n);
    TEST_ASSERT_EQUAL_UINT32(1U, dropped);

    n = alarm_registry_get_session_journal(codes, ALARM_SESSION_JOURNAL_MAX, &dropped);
    TEST_ASSERT_EQUAL_UINT(ALARM_SESSION_JOURNAL_MAX, n);
    TEST_ASSERT_EQUAL_UINT32(1U, dropped);

    alarm_registry_on_wash_session_started();
    n = alarm_registry_get_session_journal(codes, ALARM_SESSION_JOURNAL_MAX, &dropped);
    TEST_ASSERT_EQUAL_UINT(0U, n);
    TEST_ASSERT_EQUAL_UINT32(0U, dropped);
}

/* 合并读接口一次返回四项，且与单项查询口径一致 */
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

    /* 单项查询走的是同一次扫描，口径必须一致 */
    TEST_ASSERT_EQUAL_INT(blocking, alarm_registry_has_blocking_active());
    TEST_ASSERT_EQUAL_INT(alarm_registry_safety_posture(), posture);
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

    WDF_RUN_TEST(test_alarm_code_helpers_make_decode_and_validate, "", "验证报警编码辅助函数构造解码并校验");
    WDF_RUN_TEST(test_trigger_clear_idempotent, "", "验证触发源清除幂等");
    WDF_RUN_TEST(test_major_not_lockout, "", "验证重大级未锁定");
    WDF_RUN_TEST(test_critical_lockout, "", "验证严重级锁定");
    WDF_RUN_TEST(test_reset_requires_manual_condition_clear, "", "验证复位要求手动条件清除");
    WDF_RUN_TEST(test_manual_condition_clear_waits_for_reset, "", "验证手动清除条件后等待复位");
    WDF_RUN_TEST(test_reevaluate_by_group, "", "验证重新评估按分组");
    WDF_RUN_TEST(test_pull_events, "", "验证拉取事件");
    WDF_RUN_TEST(test_pending_overflow_is_reported_then_cleared,
                 "ALRM-13",
                 "验证待发事件溢出被计数上报且取走即清零");
    WDF_RUN_TEST(test_load_catalog_rejects_invalid_defs, "ALRM-15", "验证非法报警目录整表拒绝");
    WDF_RUN_TEST(test_load_catalog_resets_session_journal, "ALRM-16", "验证换目录同时清空会话日志");
    WDF_RUN_TEST(test_session_journal_blocking_levels, "", "验证会话日志仅记录阻断级别报警");
    WDF_RUN_TEST(test_active_pool_full_rejects, "ALRM-08", "验证活动报警池已满时拒绝新报警");
    WDF_RUN_TEST(test_lockout_evicts_oldest_minor_when_pool_full, "ALRM-20", "验证池满时 CRITICAL 驱逐最早 MINOR");
    WDF_RUN_TEST(test_lockout_evicts_oldest_major_when_no_minor, "ALRM-20", "验证无 MINOR 时 CRITICAL 驱逐最早 MAJOR");
    WDF_RUN_TEST(test_lockout_rejected_when_pool_full_of_lockout, "ALRM-20", "验证满员 CRITICAL 时拒绝新 CRITICAL");
    WDF_RUN_TEST(test_retrigger_active_lockout_does_not_evict, "ALRM-20", "验证池满时再触发已活跃 CRITICAL 不驱逐");
    WDF_RUN_TEST(test_session_journal_overflow_is_counted_and_not_cleared_on_read,
                 "ALRM-21",
                 "验证会话日志满池丢弃可观测且读取不清零");
    WDF_RUN_TEST(test_copy_safety_view_returns_consistent_snapshot, "", "验证复制安全视图返回一致的快照");
    WDF_RUN_TEST(test_copy_safety_view_handles_empty_and_null, "", "验证复制安全视图处理空并空指针");
    WDF_RUN_TEST(test_copy_safety_view_truncates_list_but_not_aggregates, "", "验证安全视图仅截断列表而不影响聚合值");
    WDF_RUN_TEST(test_unknown_code_rejected, "", "验证未知编码被拒绝");

    return UNITY_END();
}
