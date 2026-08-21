/**
 * @file    test_periodic_deadline.c
 * @brief   周期任务时间推进单元测试（契约 SCHED-01 / SCHED-02）
 *
 * @note    被测对象是纯函数 periodic_task_next_deadline，输入输出全部显式给定，
 *          不依赖真实时钟与 sleep，因此这两条时序行为可被确定验证而非概率验证。
 */

#include "runtime/scheduler/periodic_task.h"
#include "wdf_test_spec.h"

#include <string.h>

#define NS_PER_MS  1000000L
#define NS_PER_SEC 1000000000L

void setUp(void)
{
}

void tearDown(void)
{
}

/** @brief 构造绝对时间，允许 nsec 超过一秒由本函数归一化 */
static struct timespec make_ts(time_t sec, long nsec)
{
    struct timespec ts;

    ts.tv_sec  = sec + (nsec / NS_PER_SEC);
    ts.tv_nsec = nsec % NS_PER_SEC;
    return ts;
}

/** @brief 返回 a - b 的纳秒差，供断言比较间隔 */
static long long diff_ns(const struct timespec *a, const struct timespec *b)
{
    return ((long long)a->tv_sec - (long long)b->tv_sec) * NS_PER_SEC + ((long long)a->tv_nsec - b->tv_nsec);
}

/* --- SCHED-01：回调耗时不累加进周期 --- */

/** @brief 未超时时只推进一个周期，不跳拍 */
static void test_no_overrun_advances_exactly_one_period(void)
{
    struct timespec base     = make_ts(100, 0);
    struct timespec deadline = base;
    struct timespec now      = make_ts(100, 3 * NS_PER_MS); /* 回调耗时 3ms，小于周期 */
    uint32_t        skipped;

    skipped = periodic_task_next_deadline(&deadline, 10U, &now);

    TEST_ASSERT_EQUAL_UINT32(0U, skipped);
    TEST_ASSERT_EQUAL_INT64(10 * NS_PER_MS, diff_ns(&deadline, &base));
}

/**
 * @brief 连续多拍且每拍都有回调耗时，间隔仍严格等于周期
 * @note  这是"不漂移"的核心断言：若实现改成 now + period 的相对推进，
 *        每拍都会多出回调耗时，累计 20 拍后偏差达 60ms，本用例会失败。
 */
static void test_repeated_ticks_do_not_drift(void)
{
    struct timespec base     = make_ts(50, 0);
    struct timespec deadline = base;
    unsigned        i;

    for (i = 0U; i < 20U; i++) {
        /* 每拍回调都在截止时间后 3ms 才结束 */
        struct timespec now = deadline;

        now.tv_nsec += 3 * NS_PER_MS;
        now = make_ts(now.tv_sec, now.tv_nsec);
        TEST_ASSERT_EQUAL_UINT32(0U, periodic_task_next_deadline(&deadline, 10U, &now));
    }

    TEST_ASSERT_EQUAL_INT64(20LL * 10 * NS_PER_MS, diff_ns(&deadline, &base));
}

/* --- SCHED-02：超时跳拍而不追赶 --- */

/** @brief 回调耗时 35ms（周期 10ms）时跳过 3 拍，落到第 4 拍 */
static void test_overrun_skips_missed_ticks(void)
{
    struct timespec base     = make_ts(10, 0);
    struct timespec deadline = base;
    struct timespec now      = make_ts(10, 35 * NS_PER_MS);
    uint32_t        skipped;

    skipped = periodic_task_next_deadline(&deadline, 10U, &now);

    TEST_ASSERT_EQUAL_UINT32(3U, skipped);
    /* 关键：结果是 40ms 而非 10ms——错过的 3 拍被丢弃，不会连续补跑 */
    TEST_ASSERT_EQUAL_INT64(40 * NS_PER_MS, diff_ns(&deadline, &base));
    TEST_ASSERT_TRUE(diff_ns(&deadline, &now) > 0);
}

/** @brief 长时间停顿后只跳一次到未来，跳拍数与停顿时长成正比 */
static void test_long_stall_skips_proportionally(void)
{
    struct timespec base     = make_ts(0, 0);
    struct timespec deadline = base;
    struct timespec now      = make_ts(5, 0); /* 停顿 5s，周期 100ms */
    uint32_t        skipped;

    skipped = periodic_task_next_deadline(&deadline, 100U, &now);

    TEST_ASSERT_EQUAL_UINT32(50U, skipped);
    TEST_ASSERT_EQUAL_INT64(5100LL * NS_PER_MS, diff_ns(&deadline, &base));
}

/**
 * @brief 回调恰好在截止时间结束时也跳一拍
 * @note  边界取"不晚于即跳"：若此时不跳，clock_nanosleep 会立即返回，
 *        回调背靠背执行，等于丢掉整拍的间隔。
 */
static void test_now_equal_to_deadline_skips_one(void)
{
    struct timespec base     = make_ts(7, 0);
    struct timespec deadline = base;
    struct timespec now      = make_ts(7, 10 * NS_PER_MS);
    uint32_t        skipped;

    skipped = periodic_task_next_deadline(&deadline, 10U, &now);

    TEST_ASSERT_EQUAL_UINT32(1U, skipped);
    TEST_ASSERT_EQUAL_INT64(20 * NS_PER_MS, diff_ns(&deadline, &base));
}

/* --- 时间字段进位与参数校验 --- */

/** @brief 跨秒进位后 tv_nsec 必须保持在合法范围内 */
static void test_nsec_carry_normalizes(void)
{
    struct timespec base     = make_ts(3, 995 * NS_PER_MS);
    struct timespec deadline = base;
    struct timespec now      = make_ts(3, 990 * NS_PER_MS);

    TEST_ASSERT_EQUAL_UINT32(0U, periodic_task_next_deadline(&deadline, 10U, &now));
    TEST_ASSERT_EQUAL_INT(4, (int)deadline.tv_sec);
    TEST_ASSERT_EQUAL_INT64(5 * NS_PER_MS, (long long)deadline.tv_nsec);
    TEST_ASSERT_TRUE(deadline.tv_nsec >= 0 && deadline.tv_nsec < NS_PER_SEC);
}

/** @brief 周期不小于一秒时按秒与纳秒正确拆分 */
static void test_period_over_one_second(void)
{
    struct timespec base     = make_ts(0, 0);
    struct timespec deadline = base;
    struct timespec now      = make_ts(0, 0);

    /* now 等于 base，推进一拍后 2500ms 已严格晚于 now，不应跳拍 */
    TEST_ASSERT_EQUAL_UINT32(0U, periodic_task_next_deadline(&deadline, 2500U, &now));
    TEST_ASSERT_EQUAL_INT(2, (int)deadline.tv_sec);
    TEST_ASSERT_EQUAL_INT64(500LL * NS_PER_MS, (long long)deadline.tv_nsec);
}

/** @brief 参数非法时不修改截止时间 */
static void test_invalid_params_leave_deadline_untouched(void)
{
    struct timespec deadline = make_ts(9, 123);
    struct timespec now      = make_ts(9, 0);

    TEST_ASSERT_EQUAL_UINT32(0U, periodic_task_next_deadline(&deadline, 0U, &now));
    TEST_ASSERT_EQUAL_INT(9, (int)deadline.tv_sec);
    TEST_ASSERT_EQUAL_INT64(123, (long long)deadline.tv_nsec);

    TEST_ASSERT_EQUAL_UINT32(0U, periodic_task_next_deadline(&deadline, 10U, NULL));
    TEST_ASSERT_EQUAL_INT64(123, (long long)deadline.tv_nsec);

    TEST_ASSERT_EQUAL_UINT32(0U, periodic_task_next_deadline(NULL, 10U, &now));
}

/* --- SCHED-08：跳拍 / 回调耗时 / 唤醒滞后累加 --- */

static void test_note_cycle_accumulates_skip_and_maxima(void)
{
    periodic_task_stats_t stats;

    memset(&stats, 0, sizeof(stats));
    periodic_task_note_cycle(&stats, 0U, 1000U, 50U);
    TEST_ASSERT_EQUAL_UINT32(1U, stats.run_count);
    TEST_ASSERT_EQUAL_UINT32(0U, stats.skip_count);
    TEST_ASSERT_EQUAL_UINT32(0U, stats.skip_max);
    TEST_ASSERT_EQUAL_UINT32(1000U, stats.last_cb_us);
    TEST_ASSERT_EQUAL_UINT32(1000U, stats.max_cb_us);
    TEST_ASSERT_EQUAL_UINT32(50U, stats.last_wake_late_us);
    TEST_ASSERT_EQUAL_UINT32(50U, stats.max_wake_late_us);

    periodic_task_note_cycle(&stats, 3U, 400U, 80U);
    TEST_ASSERT_EQUAL_UINT32(2U, stats.run_count);
    TEST_ASSERT_EQUAL_UINT32(3U, stats.skip_count);
    TEST_ASSERT_EQUAL_UINT32(3U, stats.skip_max);
    TEST_ASSERT_EQUAL_UINT32(400U, stats.last_cb_us);
    TEST_ASSERT_EQUAL_UINT32(1000U, stats.max_cb_us);
    TEST_ASSERT_EQUAL_UINT32(80U, stats.last_wake_late_us);
    TEST_ASSERT_EQUAL_UINT32(80U, stats.max_wake_late_us);

    periodic_task_note_cycle(&stats, 1U, 2000U, 10U);
    TEST_ASSERT_EQUAL_UINT32(3U, stats.run_count);
    TEST_ASSERT_EQUAL_UINT32(4U, stats.skip_count);
    TEST_ASSERT_EQUAL_UINT32(3U, stats.skip_max);
    TEST_ASSERT_EQUAL_UINT32(2000U, stats.max_cb_us);
    TEST_ASSERT_EQUAL_UINT32(80U, stats.max_wake_late_us);
}

static void test_note_cycle_null_is_noop(void)
{
    periodic_task_note_cycle(NULL, 1U, 1U, 1U);
}

int main(void)
{
    UNITY_BEGIN();
    WDF_RUN_TEST(test_no_overrun_advances_exactly_one_period, "", "验证无超期推进恰好一个周期");
    WDF_RUN_TEST(test_repeated_ticks_do_not_drift, "", "验证重复周期执行DO未漂移");
    WDF_RUN_TEST(test_overrun_skips_missed_ticks, "", "验证超期跳过错过的周期执行");
    WDF_RUN_TEST(test_long_stall_skips_proportionally, "", "验证长时间停顿跳过按比例");
    WDF_RUN_TEST(test_now_equal_to_deadline_skips_one, "", "验证当前时间等于截止时间时跳过一个周期");
    WDF_RUN_TEST(test_nsec_carry_normalizes, "", "验证NSEC进位规范化");
    WDF_RUN_TEST(test_period_over_one_second, "", "验证周期超过一个一秒");
    WDF_RUN_TEST(test_invalid_params_leave_deadline_untouched, "", "验证无效参数保持截止时间不变");
    WDF_RUN_TEST(test_note_cycle_accumulates_skip_and_maxima, "", "验证跳拍与耗时按规则累加");
    WDF_RUN_TEST(test_note_cycle_null_is_noop, "", "验证空统计指针被忽略");
    return UNITY_END();
}
