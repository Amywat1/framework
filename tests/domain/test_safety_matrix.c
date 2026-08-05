/**
 * @file    test_safety_matrix.c
 * @brief   安全行为矩阵逐格点测试
 * @author  HUWANGWEI
 * @date    2026-08-03
 *
 * @note    这些用例按矩阵格点逐一断言，而不是抽样验证：安全语义的每一格都是
 *          现场行为的直接依据，改错任意一格都会造成"该停机没停"或"不该停却停"。
 *          矩阵表若被调整，这里必须同步失败——这正是形式化的目的。
 */

#include "domain/safety/model/safety_matrix.h"
#include "wdf_test_spec.h"

void setUp(void)
{
}

void tearDown(void)
{
}

/* -------------------------------------------------------------------------
 * 等级行为矩阵：3 等级 × 3 属性，共 9 个格点全部显式断言
 *
 * | 等级     | 阻塞开洗 | LOCKOUT | 记会话日志 |
 * |----------|----------|---------|------------|
 * | MINOR    | 否       | 否      | 否         |
 * | MAJOR    | 是       | 否      | 是         |
 * | CRITICAL | 是       | 是      | 是         |
 * ------------------------------------------------------------------------- */

static void test_minor_level_grid(void)
{
    alarm_level_behaviour_t b = alarm_level_behaviour(ALARM_LEVEL_MINOR);

    TEST_ASSERT_FALSE(b.blocks_wash_start);
    TEST_ASSERT_FALSE(b.forces_lockout);
    TEST_ASSERT_FALSE(b.records_in_journal);
}

static void test_major_level_grid(void)
{
    alarm_level_behaviour_t b = alarm_level_behaviour(ALARM_LEVEL_MAJOR);

    /* MAJOR 禁开洗但不 LOCKOUT：洗车中途出现 MAJOR 允许跑完当前流程，
     * 洗完若仍活跃则由会话日志驱动进入 EXCEPTION。 */
    TEST_ASSERT_TRUE(b.blocks_wash_start);
    TEST_ASSERT_FALSE(b.forces_lockout);
    TEST_ASSERT_TRUE(b.records_in_journal);
}

static void test_critical_level_grid(void)
{
    alarm_level_behaviour_t b = alarm_level_behaviour(ALARM_LEVEL_CRITICAL);

    TEST_ASSERT_TRUE(b.blocks_wash_start);
    TEST_ASSERT_TRUE(b.forces_lockout);
    TEST_ASSERT_TRUE(b.records_in_journal);
}

/* 未知等级必须取最严格值：漏配置若默认放行，表现为"有报警仍照常开洗" */
static void test_unknown_level_is_most_restrictive(void)
{
    alarm_level_behaviour_t b = alarm_level_behaviour((alarm_level_t)99);

    TEST_ASSERT_TRUE(b.blocks_wash_start);
    TEST_ASSERT_TRUE(b.forces_lockout);
    TEST_ASSERT_TRUE(b.records_in_journal);
}

/* 便捷查询函数与矩阵表必须一致，避免两条读取路径分叉 */
static void test_level_helpers_match_matrix(void)
{
    const alarm_level_t levels[] = {ALARM_LEVEL_MINOR, ALARM_LEVEL_MAJOR, ALARM_LEVEL_CRITICAL};
    unsigned            i;

    for (i = 0U; i < sizeof(levels) / sizeof(levels[0]); i++) {
        alarm_level_behaviour_t b = alarm_level_behaviour(levels[i]);

        TEST_ASSERT_EQUAL_INT(b.blocks_wash_start, alarm_level_blocks_wash(levels[i]));
        TEST_ASSERT_EQUAL_INT(b.forces_lockout, alarm_level_forces_lockout(levels[i]));
        TEST_ASSERT_EQUAL_INT(b.records_in_journal, alarm_level_records_in_journal(levels[i]));
    }
}

/* -------------------------------------------------------------------------
 * 清除策略矩阵：3 策略 × 3 属性，共 9 个格点
 *
 * | 策略         | 条件消失即清 | 需运动重评估 | 允许手动复位 |
 * |--------------|--------------|--------------|--------------|
 * | AUTO_STATIC  | 是           | 否           | 否           |
 * | ON_MOTION    | 否           | 是           | 是           |
 * | MANUAL_RESET | 否           | 否           | 是           |
 * ------------------------------------------------------------------------- */

static void test_auto_static_clear_grid(void)
{
    alarm_clear_behaviour_t b = alarm_clear_behaviour(ALARM_CLEAR_AUTO_STATIC);

    TEST_ASSERT_TRUE(b.clears_when_condition_gone);
    TEST_ASSERT_FALSE(b.needs_motion_reeval);
    TEST_ASSERT_FALSE(b.allows_manual_reset);
}

static void test_on_motion_clear_grid(void)
{
    alarm_clear_behaviour_t b = alarm_clear_behaviour(ALARM_CLEAR_ON_MOTION);

    /* 两列同真：正常由运动重评估清除，运动一直不发生时手动复位兜底，
     * 否则报警会永久卡住。 */
    TEST_ASSERT_FALSE(b.clears_when_condition_gone);
    TEST_ASSERT_TRUE(b.needs_motion_reeval);
    TEST_ASSERT_TRUE(b.allows_manual_reset);
}

static void test_manual_reset_clear_grid(void)
{
    alarm_clear_behaviour_t b = alarm_clear_behaviour(ALARM_CLEAR_MANUAL_RESET);

    TEST_ASSERT_FALSE(b.clears_when_condition_gone);
    TEST_ASSERT_FALSE(b.needs_motion_reeval);
    TEST_ASSERT_TRUE(b.allows_manual_reset);
}

/* 未知策略不得自动清除：那等于让未定义行为决定安全状态何时解除 */
static void test_unknown_clear_never_auto(void)
{
    alarm_clear_behaviour_t b = alarm_clear_behaviour((alarm_clear_t)99);

    TEST_ASSERT_FALSE(b.clears_when_condition_gone);
    TEST_ASSERT_FALSE(b.needs_motion_reeval);
    TEST_ASSERT_TRUE(b.allows_manual_reset);
}

static void test_clear_helpers_match_matrix(void)
{
    const alarm_clear_t clears[] = {ALARM_CLEAR_AUTO_STATIC, ALARM_CLEAR_ON_MOTION, ALARM_CLEAR_MANUAL_RESET};
    unsigned            i;

    for (i = 0U; i < sizeof(clears) / sizeof(clears[0]); i++) {
        alarm_clear_behaviour_t b = alarm_clear_behaviour(clears[i]);

        TEST_ASSERT_EQUAL_INT(b.clears_when_condition_gone, alarm_clear_is_auto(clears[i]));
        TEST_ASSERT_EQUAL_INT(b.needs_motion_reeval, alarm_clear_needs_motion_reeval(clears[i]));
        TEST_ASSERT_EQUAL_INT(b.allows_manual_reset, alarm_clear_allows_manual_reset(clears[i]));
    }
}

/* -------------------------------------------------------------------------
 * 等级 → 安全姿态推导
 * ------------------------------------------------------------------------- */

static void test_posture_derivation(void)
{
    /* 无活跃报警：无论传入什么等级都应为 NOMINAL */
    TEST_ASSERT_EQUAL_INT(SAFETY_POSTURE_NOMINAL, safety_posture_from_level(ALARM_LEVEL_CRITICAL, false));

    TEST_ASSERT_EQUAL_INT(SAFETY_POSTURE_NOMINAL, safety_posture_from_level(ALARM_LEVEL_MINOR, true));
    TEST_ASSERT_EQUAL_INT(SAFETY_POSTURE_NOMINAL, safety_posture_from_level(ALARM_LEVEL_MAJOR, true));
    TEST_ASSERT_EQUAL_INT(SAFETY_POSTURE_LOCKOUT, safety_posture_from_level(ALARM_LEVEL_CRITICAL, true));
}

/* -------------------------------------------------------------------------
 * 跨矩阵一致性：等级严重度必须单调
 *
 * 更高等级不得比更低等级宽松。这条不变量比单个格点更重要：
 * 新增等级时若插在中间且属性配错，逐格断言可能仍然通过，但单调性会失败。
 * ------------------------------------------------------------------------- */
static void test_severity_is_monotonic(void)
{
    const alarm_level_t ordered[] = {ALARM_LEVEL_MINOR, ALARM_LEVEL_MAJOR, ALARM_LEVEL_CRITICAL};
    unsigned            i;

    for (i = 1U; i < sizeof(ordered) / sizeof(ordered[0]); i++) {
        alarm_level_behaviour_t lower  = alarm_level_behaviour(ordered[i - 1U]);
        alarm_level_behaviour_t higher = alarm_level_behaviour(ordered[i]);

        /* 低等级为真的属性，高等级必须也为真 */
        if (lower.blocks_wash_start) {
            TEST_ASSERT_TRUE(higher.blocks_wash_start);
        }
        if (lower.forces_lockout) {
            TEST_ASSERT_TRUE(higher.forces_lockout);
        }
        if (lower.records_in_journal) {
            TEST_ASSERT_TRUE(higher.records_in_journal);
        }
    }
}

int main(void)
{
    UNITY_BEGIN();
    WDF_RUN_TEST(test_minor_level_grid, "", "验证轻微级报警行为矩阵");
    WDF_RUN_TEST(test_major_level_grid, "", "验证重大级报警行为矩阵");
    WDF_RUN_TEST(test_critical_level_grid, "", "验证严重级报警行为矩阵");
    WDF_RUN_TEST(test_unknown_level_is_most_restrictive, "", "验证未知级别为最严格限制");
    WDF_RUN_TEST(test_level_helpers_match_matrix, "", "验证级别辅助函数与安全矩阵一致");
    WDF_RUN_TEST(test_auto_static_clear_grid, "", "验证自动静止时清除组合矩阵");
    WDF_RUN_TEST(test_on_motion_clear_grid, "", "验证开启运动时清除组合矩阵");
    WDF_RUN_TEST(test_manual_reset_clear_grid, "", "验证手动复位清除组合矩阵");
    WDF_RUN_TEST(test_unknown_clear_never_auto, "", "验证未知清除永不自动");
    WDF_RUN_TEST(test_clear_helpers_match_matrix, "", "验证清除辅助函数与安全矩阵一致");
    WDF_RUN_TEST(test_posture_derivation, "", "验证安全姿态推导");
    WDF_RUN_TEST(test_severity_is_monotonic, "", "验证报警严重度保持单调");
    return UNITY_END();
}
