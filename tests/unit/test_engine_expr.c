/**
 * @file    test_engine_expr.c
 * @brief   引擎表达式编译/求值单元测试
 * @author  huwangwei
 * @date    2026-06-25
 */

#include "domain/engine/engine_expr.h"
#include "unity.h"

#include <stdio.h>
#include <string.h>
#include <math.h>

/* -------------------------------------------------------------------------
 * 测试用变量解析器：用一张固定的名值表
 * ------------------------------------------------------------------------- */
typedef struct
{
    const char *name;
    double      value;
    bool        ok;   /* false 表示该名"未知"，用于测试解析失败 */
} var_row_t;

static const var_row_t s_vars[] = {
    { "$gantry_tail_min_pos",          150.0, true },
    { "$gantry_side_stop_offset",       60.0, true },
    { "ESTOP",                           0.0, true },
    { "LIFT_UP_LIMIT",                   1.0, true },
    { "GANTRY_FWD_LIMIT",                0.0, true },
    { "axes.gantry.position",          200.0, true },
    { "axes.gantry.valid",               1.0, true },
    { "markers.car_tail.position",     180.0, true },
    { "markers.car_tail.valid",          1.0, true },
    { "phase.elapsed_ms",             3000.0, true },
};

static bool test_resolve(void *ctx, const char *name, double *out_value)
{
    (void)ctx;
    for (unsigned i = 0U; i < (sizeof(s_vars) / sizeof(s_vars[0])); ++i)
    {
        if (strcmp(s_vars[i].name, name) == 0)
        {
            *out_value = s_vars[i].value;
            return s_vars[i].ok;
        }
    }
    return false; /* 未知变量 */
}

static const engine_expr_env_t s_env = { test_resolve, NULL };

/* 求值布尔的便捷封装：要求成功 */
static bool eval_b(const char *text)
{
    engine_expr_t *e = engine_expr_compile(text);
    TEST_ASSERT_NOT_NULL(e);
    bool ok = false;
    bool v  = engine_expr_eval_bool(e, &s_env, &ok);
    TEST_ASSERT_TRUE(ok);
    engine_expr_free(e);
    return v;
}

/* 求值数值：要求成功 */
static double eval_n(const char *text)
{
    engine_expr_t *e = engine_expr_compile(text);
    TEST_ASSERT_NOT_NULL(e);
    bool ok = false;
    double v = engine_expr_eval(e, &s_env, &ok);
    TEST_ASSERT_TRUE(ok);
    engine_expr_free(e);
    return v;
}

void setUp(void)    {}
void tearDown(void) {}

static void test_arithmetic(void)
{
    TEST_ASSERT_DOUBLE_WITHIN(1e-9, 210.0, eval_n("150 + 60"));
    TEST_ASSERT_DOUBLE_WITHIN(1e-9, 100.0, eval_n("$gantry_tail_min_pos - 50"));
    TEST_ASSERT_DOUBLE_WITHIN(1e-9,  14.0, eval_n("2 + 3 * 4"));        /* 优先级 */
    TEST_ASSERT_DOUBLE_WITHIN(1e-9,  20.0, eval_n("(2 + 3) * 4"));      /* 括号 */
    TEST_ASSERT_DOUBLE_WITHIN(1e-9,   2.5, eval_n("10 / 4"));
    TEST_ASSERT_DOUBLE_WITHIN(1e-9,   0.0, eval_n("10 / 0"));           /* 除零保护 */
    TEST_ASSERT_DOUBLE_WITHIN(1e-9,   3.0, eval_n("-5 + 8"));           /* 一元负号 */
}

static void test_comparison(void)
{
    TEST_ASSERT_TRUE(eval_b("1 == 1"));
    TEST_ASSERT_TRUE(eval_b("2 != 3"));
    TEST_ASSERT_TRUE(eval_b("3 > 2"));
    TEST_ASSERT_TRUE(eval_b("2 < 3"));
    TEST_ASSERT_TRUE(eval_b("3 >= 3"));
    TEST_ASSERT_TRUE(eval_b("2 <= 3"));
    TEST_ASSERT_FALSE(eval_b("3 < 2"));
    TEST_ASSERT_FALSE(eval_b("3 == 2"));
}

static void test_logic(void)
{
    TEST_ASSERT_TRUE(eval_b("1 AND 1"));
    TEST_ASSERT_FALSE(eval_b("1 AND 0"));
    TEST_ASSERT_TRUE(eval_b("0 OR 1"));
    TEST_ASSERT_FALSE(eval_b("0 OR 0"));
    TEST_ASSERT_TRUE(eval_b("NOT 0"));
    TEST_ASSERT_FALSE(eval_b("NOT 1"));
    /* 优先级：AND 高于 OR */
    TEST_ASSERT_TRUE(eval_b("0 AND 1 OR 1"));
    TEST_ASSERT_FALSE(eval_b("0 AND (1 OR 1)"));
    /* NOT 与比较组合 */
    TEST_ASSERT_TRUE(eval_b("NOT (3 < 2)"));
}

static void test_ternary(void)
{
    TEST_ASSERT_DOUBLE_WITHIN(1e-9, 10.0, eval_n("1 ? 10 : 20"));
    TEST_ASSERT_DOUBLE_WITHIN(1e-9, 20.0, eval_n("0 ? 10 : 20"));
    TEST_ASSERT_DOUBLE_WITHIN(1e-9,  1.0, eval_n("3 > 2 ? 1 : 0"));
}

static void test_variables(void)
{
    TEST_ASSERT_TRUE(eval_b("ESTOP == 0"));
    TEST_ASSERT_TRUE(eval_b("LIFT_UP_LIMIT == 1"));
    TEST_ASSERT_TRUE(eval_b("axes.gantry.position >= $gantry_tail_min_pos"));
    TEST_ASSERT_TRUE(eval_b("markers.car_tail.valid"));
    TEST_ASSERT_TRUE(eval_b("phase.elapsed_ms >= 3000"));
    /* 折叠标量：200 >= 180 + 60(=240) 为假 */
    TEST_ASSERT_FALSE(eval_b("markers.car_tail.valid AND "
                             "axes.gantry.position >= markers.car_tail.position + "
                             "$gantry_side_stop_offset"));
    TEST_ASSERT_FALSE(eval_b("axes.gantry.position >= markers.car_tail.position + 60"));
    /* 偏移改小则成立：200 >= 180 + 10(=190) 为真 */
    TEST_ASSERT_TRUE(eval_b("markers.car_tail.valid AND "
                            "axes.gantry.position >= markers.car_tail.position + 10"));
}

static void test_errors(void)
{
    /* 语法错误：编译失败返回 NULL */
    TEST_ASSERT_NULL(engine_expr_compile("1 +"));
    TEST_ASSERT_NULL(engine_expr_compile("(1 + 2"));
    TEST_ASSERT_NULL(engine_expr_compile("1 ? 2"));
    TEST_ASSERT_NULL(engine_expr_compile(""));
    TEST_ASSERT_NULL(engine_expr_compile("@"));

    /* 未知变量：编译成功，但求值失败 */
    engine_expr_t *e = engine_expr_compile("UNKNOWN_SIGNAL == 1");
    TEST_ASSERT_NOT_NULL(e);
    bool ok = true;
    (void)engine_expr_eval(e, &s_env, &ok);
    TEST_ASSERT_FALSE(ok);
    engine_expr_free(e);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_arithmetic);
    RUN_TEST(test_comparison);
    RUN_TEST(test_logic);
    RUN_TEST(test_ternary);
    RUN_TEST(test_variables);
    RUN_TEST(test_errors);
    return UNITY_END();
}
