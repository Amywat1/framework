/**
 * @file    test_contactor_switch.c
 * @brief   接触器切换时序模块单元测试
 *
 * 分组：
 *   A. 初始化参数校验
 *   B. 目标已就位（无需动作）
 *   C. 切换全流程（release_ms/close_ms 均为 0，逐次 tick 立即推进）
 *   D. 切换全流程（release_ms/close_ms 非零，验证按时间戳分阶段推进）
 *   E. 等待期间目标变更（RELEASING / ENGAGING 阶段中途改目标）
 *   F. 查询接口
 *
 * @note    now_ms 由测试用例显式传入固定序列，不依赖挂钟时间，
 *          时序验证完全确定。
 */

#include "domain/device/mechanism/contactor_switch.h"
#include "unity.h"

#include <stddef.h>

/* -------------------------------------------------------------------------
 * 接触器回调 mock
 * ------------------------------------------------------------------------- */
static int s_engage_calls;
static int s_last_engage_id;
static int s_release_calls;
static int s_last_release_id;

static void mock_engage(void *ctx, int output_id)
{
    (void)ctx;
    s_engage_calls++;
    s_last_engage_id = output_id;
}

static void mock_release(void *ctx, int output_id)
{
    (void)ctx;
    s_release_calls++;
    s_last_release_id = output_id;
}

static const contactor_ops_t s_ops = {
    .engage  = mock_engage,
    .release = mock_release,
    .ctx     = NULL,
};

static void reset_mock_counters(void)
{
    s_engage_calls    = 0;
    s_last_engage_id  = -1;
    s_release_calls   = 0;
    s_last_release_id = -1;
}

static contactor_switch_t s_cs;

void setUp(void)
{
    reset_mock_counters();
}

void tearDown(void) {}

/* =========================================================================
 * A. 初始化参数校验
 * ========================================================================= */

void test_init_null_cs(void)
{
    contactor_timing_t timing = {0U, 0U};
    TEST_ASSERT_EQUAL(SW_ERR_PARAM, contactor_switch_init(NULL, &s_ops, timing, 0));
}

void test_init_null_ops(void)
{
    contactor_timing_t timing = {0U, 0U};
    TEST_ASSERT_EQUAL(SW_ERR_PARAM, contactor_switch_init(&s_cs, NULL, timing, 0));
}

void test_init_null_engage(void)
{
    contactor_ops_t     ops = s_ops;
    contactor_timing_t timing = {0U, 0U};
    ops.engage = NULL;
    TEST_ASSERT_EQUAL(SW_ERR_PARAM, contactor_switch_init(&s_cs, &ops, timing, 0));
}

void test_init_null_release(void)
{
    contactor_ops_t     ops = s_ops;
    contactor_timing_t timing = {0U, 0U};
    ops.release = NULL;
    TEST_ASSERT_EQUAL(SW_ERR_PARAM, contactor_switch_init(&s_cs, &ops, timing, 0));
}

void test_init_ok(void)
{
    contactor_timing_t timing = {0U, 0U};
    TEST_ASSERT_EQUAL(SW_OK, contactor_switch_init(&s_cs, &s_ops, timing, 0));
}

void test_init_sets_current_id(void)
{
    contactor_timing_t timing = {0U, 0U};
    contactor_switch_init(&s_cs, &s_ops, timing, 1);
    TEST_ASSERT_EQUAL(1, contactor_switch_current(&s_cs));
}

/* =========================================================================
 * B. 目标已就位（无需动作）
 * ========================================================================= */

void test_prepare_same_target_returns_true_immediately(void)
{
    contactor_timing_t timing = {200U, 300U};
    contactor_switch_init(&s_cs, &s_ops, timing, 0);

    TEST_ASSERT_TRUE(contactor_switch_prepare(&s_cs, 0, 1000U));
    TEST_ASSERT_EQUAL(0, s_engage_calls);
    TEST_ASSERT_EQUAL(0, s_release_calls);
}

/* =========================================================================
 * C. 切换全流程（延时均为 0，逐次调用立即推进）
 * ========================================================================= */

void test_switch_zero_delay_sequence(void)
{
    contactor_timing_t timing = {0U, 0U};
    contactor_switch_init(&s_cs, &s_ops, timing, 0);

    /* 第 1 次：IDLE → RELEASING，触发 release(0) */
    TEST_ASSERT_FALSE(contactor_switch_prepare(&s_cs, 1, 0U));
    TEST_ASSERT_EQUAL(1, s_release_calls);
    TEST_ASSERT_EQUAL(0, s_last_release_id);
    TEST_ASSERT_EQUAL(0, s_engage_calls);

    /* 第 2 次：RELEASING → ENGAGING（release_ms=0 立即满足），触发 engage(1) */
    TEST_ASSERT_FALSE(contactor_switch_prepare(&s_cs, 1, 0U));
    TEST_ASSERT_EQUAL(1, s_engage_calls);
    TEST_ASSERT_EQUAL(1, s_last_engage_id);
    TEST_ASSERT_EQUAL(1, contactor_switch_current(&s_cs));

    /* 第 3 次：ENGAGING → IDLE（close_ms=0 立即满足），返回 true */
    TEST_ASSERT_TRUE(contactor_switch_prepare(&s_cs, 1, 0U));
    TEST_ASSERT_EQUAL(1, s_engage_calls);
    TEST_ASSERT_EQUAL(1, s_release_calls);
}

/* =========================================================================
 * D. 切换全流程（延时非零，验证按时间戳分阶段推进）
 * ========================================================================= */

void test_switch_nonzero_delay_stays_pending_before_deadline(void)
{
    contactor_timing_t timing = {200U, 300U};
    contactor_switch_init(&s_cs, &s_ops, timing, 0);

    /* t=0：IDLE → RELEASING */
    TEST_ASSERT_FALSE(contactor_switch_prepare(&s_cs, 1, 0U));
    TEST_ASSERT_EQUAL(1, s_release_calls);

    /* t=100：release_ms=200 尚未到，仍停留 RELEASING，不触发 engage */
    TEST_ASSERT_FALSE(contactor_switch_prepare(&s_cs, 1, 100U));
    TEST_ASSERT_EQUAL(0, s_engage_calls);
}

void test_switch_nonzero_delay_full_sequence(void)
{
    contactor_timing_t timing = {200U, 300U};
    contactor_switch_init(&s_cs, &s_ops, timing, 0);

    TEST_ASSERT_FALSE(contactor_switch_prepare(&s_cs, 1, 0U));      /* → RELEASING，until=200 */
    TEST_ASSERT_FALSE(contactor_switch_prepare(&s_cs, 1, 199U));    /* 未到 200，仍 RELEASING */
    TEST_ASSERT_EQUAL(0, s_engage_calls);

    TEST_ASSERT_FALSE(contactor_switch_prepare(&s_cs, 1, 200U));    /* 到达 → ENGAGING，until=500 */
    TEST_ASSERT_EQUAL(1, s_engage_calls);
    TEST_ASSERT_EQUAL(1, s_last_engage_id);

    TEST_ASSERT_FALSE(contactor_switch_prepare(&s_cs, 1, 499U));    /* 未到 500，仍 ENGAGING */
    TEST_ASSERT_TRUE(contactor_switch_prepare(&s_cs, 1, 500U));     /* 到达 → 完成 */

    TEST_ASSERT_EQUAL(1, contactor_switch_current(&s_cs));
    TEST_ASSERT_EQUAL(1, s_release_calls);
    TEST_ASSERT_EQUAL(1, s_engage_calls);
}

/* =========================================================================
 * E. 等待期间目标变更
 * ========================================================================= */

void test_target_change_during_releasing_engages_new_target(void)
{
    contactor_timing_t timing = {200U, 300U};
    contactor_switch_init(&s_cs, &s_ops, timing, 0);

    TEST_ASSERT_FALSE(contactor_switch_prepare(&s_cs, 1, 0U));   /* 请求切到 1 → RELEASING */
    /* 等待期间改主意，请求切到 1 依旧（同 id，验证不受影响的基线） */
    TEST_ASSERT_FALSE(contactor_switch_prepare(&s_cs, 1, 200U)); /* → ENGAGING(1) */
    TEST_ASSERT_EQUAL(1, s_last_engage_id);

    /* 在 ENGAGING(1) 等待期间又改成目标 0：应重新触发 release(1) → RELEASING */
    TEST_ASSERT_FALSE(contactor_switch_prepare(&s_cs, 0, 250U));
    TEST_ASSERT_EQUAL(2, s_release_calls);
    TEST_ASSERT_EQUAL(1, s_last_release_id);

    /* release_ms 从 250 开始重新计时，到 450 才满足 */
    TEST_ASSERT_FALSE(contactor_switch_prepare(&s_cs, 0, 449U));
    TEST_ASSERT_EQUAL(1, s_engage_calls);
    TEST_ASSERT_FALSE(contactor_switch_prepare(&s_cs, 0, 450U)); /* → ENGAGING(0) */
    TEST_ASSERT_EQUAL(2, s_engage_calls);
    TEST_ASSERT_EQUAL(0, s_last_engage_id);

    TEST_ASSERT_TRUE(contactor_switch_prepare(&s_cs, 0, 750U));  /* close_ms=300 → 完成 */
    TEST_ASSERT_EQUAL(0, contactor_switch_current(&s_cs));
}

/* =========================================================================
 * F. 查询接口
 * ========================================================================= */

void test_current_returns_minus_one_for_null(void)
{
    TEST_ASSERT_EQUAL(-1, contactor_switch_current(NULL));
}

/* =========================================================================
 * main
 * ========================================================================= */

int main(void)
{
    UNITY_BEGIN();

    /* A. 初始化参数校验 */
    RUN_TEST(test_init_null_cs);
    RUN_TEST(test_init_null_ops);
    RUN_TEST(test_init_null_engage);
    RUN_TEST(test_init_null_release);
    RUN_TEST(test_init_ok);
    RUN_TEST(test_init_sets_current_id);

    /* B. 目标已就位 */
    RUN_TEST(test_prepare_same_target_returns_true_immediately);

    /* C. 切换全流程（延时为 0） */
    RUN_TEST(test_switch_zero_delay_sequence);

    /* D. 切换全流程（延时非零） */
    RUN_TEST(test_switch_nonzero_delay_stays_pending_before_deadline);
    RUN_TEST(test_switch_nonzero_delay_full_sequence);

    /* E. 等待期间目标变更 */
    RUN_TEST(test_target_change_during_releasing_engages_new_target);

    /* F. 查询接口 */
    RUN_TEST(test_current_returns_minus_one_for_null);

    return UNITY_END();
}
