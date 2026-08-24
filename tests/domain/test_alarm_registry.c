/**
 * @file    test_alarm_registry.c
 * @brief   alarm_registry 单元测试
 */

#include "common/event_types.h"
#include "common/sw_error.h"
#include "common/time_util.h"
#include "domain/safety/alarm_registry/alarm_registry.h"
#include "domain/safety/model/alarm_types.h"
#include "runtime/event_bus/event_bus.h"
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
    {
     .code         = 901001U,
     .level        = ALARM_LEVEL_MINOR,
     .clear        = ALARM_CLEAR_AUTO_STATIC,
     .reeval_group = ALARM_REEVAL_GROUP_NONE,
     .desc         = "demo minor",
     },
};

static uint32_t s_trig[ALARM_ACTIVE_MAX];
static unsigned s_trig_n;
static uint32_t s_clr[ALARM_ACTIVE_MAX];
static unsigned s_clr_n;
static int      s_lockout_n;
static int      s_nominal_n;

static void on_trig(const event_t *evt)
{
    if (s_trig_n < ALARM_ACTIVE_MAX) {
        s_trig[s_trig_n++] = evt->param;
    } else {
        s_trig_n++;
    }
}

static void on_clr(const event_t *evt)
{
    if (s_clr_n < ALARM_ACTIVE_MAX) {
        s_clr[s_clr_n++] = evt->param;
    } else {
        s_clr_n++;
    }
}

static void on_lockout(const event_t *evt)
{
    (void)evt;
    s_lockout_n++;
}

static void on_nominal(const event_t *evt)
{
    (void)evt;
    s_nominal_n++;
}

static void reset_bus_counts(void)
{
    s_trig_n     = 0U;
    s_clr_n      = 0U;
    s_lockout_n  = 0;
    s_nominal_n  = 0;
}

static void drain_bus(void)
{
    TEST_ASSERT_EQUAL_INT(SW_OK, event_bus_drain());
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
    time_util_init();
    TEST_ASSERT_EQUAL_INT(SW_OK, event_bus_init());
    alarm_registry_init();
    (void)alarm_registry_load_catalog(s_catalog, 3U);
    reset_bus_counts();
    (void)event_subscribe(EVT_ALARM_TRIGGERED, on_trig);
    (void)event_subscribe(EVT_ALARM_CLEARED, on_clr);
    (void)event_subscribe(EVT_SAFETY_LOCKOUT, on_lockout);
    (void)event_subscribe(EVT_SAFETY_NOMINAL, on_nominal);
}

void tearDown(void)
{
}

static void test_trigger_clear_idempotent(void)
{
    TEST_ASSERT_EQUAL_INT(SW_OK, alarm_registry_trigger(201101U));
    drain_bus();
    TEST_ASSERT_EQUAL_UINT(1U, s_trig_n);
    TEST_ASSERT_EQUAL_UINT(201101U, s_trig[0]);
    TEST_ASSERT_TRUE(alarm_registry_is_active(201101U));
    TEST_ASSERT_EQUAL_INT(SW_OK, alarm_registry_trigger(201101U));
    drain_bus();
    TEST_ASSERT_EQUAL_UINT(1U, s_trig_n); /* 重复 trigger 不重复 TRIGGERED */
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

static void test_minor_published_before_return(void)
{
    TEST_ASSERT_EQUAL_INT(SW_OK, alarm_registry_load_catalog(s_catalog, 4U));
    TEST_ASSERT_EQUAL_INT(SW_OK, alarm_registry_trigger(901001U));
    drain_bus();
    TEST_ASSERT_EQUAL_UINT(1U, s_trig_n);
    TEST_ASSERT_EQUAL_UINT(901001U, s_trig[0]);
    TEST_ASSERT_EQUAL_INT(0, s_lockout_n);
}

static void test_last_critical_clear_publishes_nominal(void)
{
    TEST_ASSERT_EQUAL_INT(SW_OK, alarm_registry_trigger(201709U));
    drain_bus();
    TEST_ASSERT_EQUAL_INT(1, s_lockout_n);
    TEST_ASSERT_EQUAL_INT(SW_OK, alarm_registry_clear(201709U));
    drain_bus();
    TEST_ASSERT_EQUAL_UINT(1U, s_clr_n);
    TEST_ASSERT_EQUAL_UINT(201709U, s_clr[0]);
    TEST_ASSERT_EQUAL_INT(1, s_nominal_n);
    TEST_ASSERT_EQUAL_INT(SAFETY_POSTURE_NOMINAL, alarm_registry_safety_posture());
}

static void test_reset_all_publishes_cleared(void)
{
    TEST_ASSERT_EQUAL_INT(SW_OK, alarm_registry_trigger(201101U));
    TEST_ASSERT_EQUAL_INT(SW_OK, alarm_registry_clear(201101U));
    drain_bus();
    reset_bus_counts();
    alarm_registry_reset_all();
    drain_bus();
    TEST_ASSERT_EQUAL_UINT(1U, s_clr_n);
    TEST_ASSERT_EQUAL_UINT(201101U, s_clr[0]);
    TEST_ASSERT_FALSE(alarm_registry_is_active(201101U));
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
    drain_bus();
    reset_bus_counts();
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
    alarm_def_t cat[ALARM_ACTIVE_MAX + 1U];
    uint32_t    crit = 201709U;
    unsigned    i;

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

    drain_bus();
    TEST_ASSERT_EQUAL_UINT(1U, s_clr_n);
    TEST_ASSERT_EQUAL_UINT(fill_code(0U), s_clr[0]);
    TEST_ASSERT_EQUAL_UINT(1U, s_trig_n);
    TEST_ASSERT_EQUAL_UINT(crit, s_trig[0]);
    TEST_ASSERT_EQUAL_INT(1, s_lockout_n);
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
    alarm_def_t cat[ALARM_ACTIVE_MAX + 1U];
    unsigned    i;

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

    drain_bus();
    TEST_ASSERT_EQUAL_UINT(0U, s_trig_n);
    TEST_ASSERT_EQUAL_UINT(0U, s_clr_n);
}

static void test_retrigger_active_lockout_does_not_evict(void)
{
    alarm_def_t cat[ALARM_ACTIVE_MAX];
    uint32_t    crit = fill_code(ALARM_ACTIVE_MAX - 1U);
    unsigned    i;

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

    drain_bus();
    TEST_ASSERT_EQUAL_UINT(0U, s_trig_n);
    TEST_ASSERT_EQUAL_UINT(0U, s_clr_n);
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
    extra                               = fill_code(ALARM_SESSION_JOURNAL_MAX);
    minor                               = ALARM_CODE_MAKE(ALM_C_SW, 50U, ALM_N_OTHER);
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

/* 合并读接口一次返回活动表、聚合值与 journal，且与单项查询口径一致 */
static void test_copy_safety_view_returns_consistent_snapshot(void)
{
    alarm_safety_view_t view;

    alarm_registry_on_wash_session_started();
    (void)alarm_registry_trigger(201101U); /* MAJOR */
    (void)alarm_registry_trigger(201709U); /* CRITICAL */

    TEST_ASSERT_EQUAL_INT(SW_OK, alarm_registry_copy_safety_view(&view));

    TEST_ASSERT_EQUAL_UINT(2U, view.count);
    TEST_ASSERT_TRUE(view.blocking);
    TEST_ASSERT_EQUAL_UINT(201709U, view.top_code); /* CRITICAL 级别最高 */
    TEST_ASSERT_EQUAL_INT(SAFETY_POSTURE_LOCKOUT, view.posture);
    TEST_ASSERT_EQUAL_UINT(2U, view.journal_count);
    TEST_ASSERT_EQUAL_UINT32(0U, view.journal_dropped);

    TEST_ASSERT_EQUAL_INT(view.blocking, alarm_registry_has_blocking_active());
    TEST_ASSERT_EQUAL_INT(alarm_registry_safety_posture(), view.posture);
}

/* 空表合法；NULL 出参返回 PARAM */
static void test_copy_safety_view_handles_empty_and_null(void)
{
    alarm_safety_view_t view;

    TEST_ASSERT_EQUAL_INT(SW_ERR_PARAM, alarm_registry_copy_safety_view(NULL));

    TEST_ASSERT_EQUAL_INT(SW_OK, alarm_registry_copy_safety_view(&view));
    TEST_ASSERT_EQUAL_UINT(0U, view.count);
    TEST_ASSERT_EQUAL_INT(SAFETY_POSTURE_NOMINAL, view.posture);
    TEST_ASSERT_FALSE(view.blocking);
    TEST_ASSERT_EQUAL_UINT(ALARM_CODE_NONE, view.top_code);
}

/* journal 丢弃计数与活动表同锁读出，连续读取不清零 */
static void test_copy_safety_view_journal_dropped_sticky(void)
{
    alarm_def_t         cat[ALARM_SESSION_JOURNAL_MAX + 1U];
    alarm_safety_view_t view;
    unsigned            i;

    for (i = 0U; i < (ALARM_SESSION_JOURNAL_MAX + 1U); ++i) {
        cat[i] = make_fill_def(fill_code(i), ALARM_LEVEL_MAJOR);
    }
    TEST_ASSERT_EQUAL_INT(SW_OK, alarm_registry_load_catalog(cat, ALARM_SESSION_JOURNAL_MAX + 1U));
    alarm_registry_on_wash_session_started();
    for (i = 0U; i < (ALARM_SESSION_JOURNAL_MAX + 1U); ++i) {
        TEST_ASSERT_EQUAL_INT(SW_OK, alarm_registry_trigger(fill_code(i)));
    }

    TEST_ASSERT_EQUAL_INT(SW_OK, alarm_registry_copy_safety_view(&view));
    TEST_ASSERT_EQUAL_UINT(ALARM_SESSION_JOURNAL_MAX, view.journal_count);
    TEST_ASSERT_EQUAL_UINT32(1U, view.journal_dropped);
    TEST_ASSERT_EQUAL_UINT(ALARM_SESSION_JOURNAL_MAX + 1U, view.count);

    TEST_ASSERT_EQUAL_INT(SW_OK, alarm_registry_copy_safety_view(&view));
    TEST_ASSERT_EQUAL_UINT32(1U, view.journal_dropped);
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
    WDF_RUN_TEST(test_minor_published_before_return, "", "验证 MINOR 触发返回前已入队");
    WDF_RUN_TEST(test_last_critical_clear_publishes_nominal, "", "验证末条 CRITICAL 清除后立即发 NOMINAL");
    WDF_RUN_TEST(test_reset_all_publishes_cleared, "", "验证 reset_all 批量 CLEARED 已入队");
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
    WDF_RUN_TEST(test_copy_safety_view_handles_empty_and_null, "", "验证复制安全视图处理空表与空指针");
    WDF_RUN_TEST(test_copy_safety_view_journal_dropped_sticky, "ALRM-21", "验证视图 journal 丢弃计数读取不清零");
    WDF_RUN_TEST(test_unknown_code_rejected, "", "验证未知编码被拒绝");

    return UNITY_END();
}
