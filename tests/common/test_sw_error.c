/**
 * @file    test_sw_error.c
 * @brief   错误码分类与数值稳定性测试
 * @author  HUWANGWEI
 * @date    2026-08-03
 *
 * @note    数值断言不是形式主义：错误码经 protobuf 跨进程传给观测进程，
 *          也进日志与云端上报。若有人删除中间枚举导致后续值前移，观测侧解读
 *          出的错误原因会全部错位。这些断言让那种改动在 CI 阶段就失败。
 */

#include "common/sw_error.h"
#include "unity.h"

#include <string.h>

void setUp(void)
{
}

void tearDown(void)
{
}

/* -------------------------------------------------------------------------
 * 数值稳定性：已分配的数值不得变动
 * ------------------------------------------------------------------------- */
static void test_error_values_are_stable(void)
{
    TEST_ASSERT_EQUAL_INT(0, SW_OK);
    TEST_ASSERT_EQUAL_INT(-1, SW_ERR_PARAM);
    TEST_ASSERT_EQUAL_INT(-2, SW_ERR_TIMEOUT);
    TEST_ASSERT_EQUAL_INT(-3, SW_ERR_HW);
    TEST_ASSERT_EQUAL_INT(-4, SW_ERR_BUSY);
    TEST_ASSERT_EQUAL_INT(-5, SW_ERR_NOMEM);
    TEST_ASSERT_EQUAL_INT(-6, SW_ERR_OVERFLOW);
    TEST_ASSERT_EQUAL_INT(-7, SW_ERR_STATE);
    TEST_ASSERT_EQUAL_INT(-8, SW_ERR_CRC);
    TEST_ASSERT_EQUAL_INT(-9, SW_ERR_STORAGE);
    TEST_ASSERT_EQUAL_INT(-10, SW_ERR_COMM);
    /* -11 与 -13 为已废弃保留值，不得被其他错误码占用 */
    TEST_ASSERT_EQUAL_INT(-12, SW_ERR_NOT_INIT);
    TEST_ASSERT_EQUAL_INT(-14, SW_ERR_NOT_FOUND);
}

/* -------------------------------------------------------------------------
 * 分类：瞬时可重试
 * ------------------------------------------------------------------------- */
static void test_transient_classification(void)
{
    TEST_ASSERT_TRUE(sw_err_is_transient(SW_ERR_BUSY));
    TEST_ASSERT_TRUE(sw_err_is_transient(SW_ERR_TIMEOUT));
    TEST_ASSERT_TRUE(sw_err_is_transient(SW_ERR_COMM));

    /* 硬件与存储失败不算瞬时：重试同一操作通常得到同样结果 */
    TEST_ASSERT_FALSE(sw_err_is_transient(SW_ERR_HW));
    TEST_ASSERT_FALSE(sw_err_is_transient(SW_ERR_STORAGE));
    TEST_ASSERT_FALSE(sw_err_is_transient(SW_ERR_CRC));
    TEST_ASSERT_FALSE(sw_err_is_transient(SW_OK));
}

/* -------------------------------------------------------------------------
 * 分类：调用方缺陷
 * ------------------------------------------------------------------------- */
static void test_caller_fault_classification(void)
{
    TEST_ASSERT_TRUE(sw_err_is_caller_fault(SW_ERR_PARAM));
    TEST_ASSERT_TRUE(sw_err_is_caller_fault(SW_ERR_STATE));
    TEST_ASSERT_TRUE(sw_err_is_caller_fault(SW_ERR_NOT_FOUND));

    TEST_ASSERT_FALSE(sw_err_is_caller_fault(SW_ERR_HW));
    TEST_ASSERT_FALSE(sw_err_is_caller_fault(SW_ERR_BUSY));
    TEST_ASSERT_FALSE(sw_err_is_caller_fault(SW_OK));
}

/* -------------------------------------------------------------------------
 * 分类：接入缺失
 * ------------------------------------------------------------------------- */
static void test_missing_binding_classification(void)
{
    TEST_ASSERT_TRUE(sw_err_is_missing_binding(SW_ERR_NOT_INIT));

    TEST_ASSERT_FALSE(sw_err_is_missing_binding(SW_ERR_PARAM));
    TEST_ASSERT_FALSE(sw_err_is_missing_binding(SW_OK));
}

/* -------------------------------------------------------------------------
 * 三类互斥：同一错误码不应同时落入多类，否则调用方无法据此决策
 * ------------------------------------------------------------------------- */
static void test_classifications_are_mutually_exclusive(void)
{
    const sw_err_t all[] = {
        SW_ERR_PARAM, SW_ERR_TIMEOUT,  SW_ERR_HW,       SW_ERR_BUSY,  SW_ERR_NOMEM,
        SW_ERR_OVERFLOW, SW_ERR_STATE, SW_ERR_CRC,      SW_ERR_STORAGE,
        SW_ERR_COMM,  SW_ERR_NOT_INIT, SW_ERR_NOT_FOUND,
    };
    unsigned i;

    for (i = 0U; i < sizeof(all) / sizeof(all[0]); i++) {
        int hits = 0;

        if (sw_err_is_transient(all[i])) { hits++; }
        if (sw_err_is_caller_fault(all[i])) { hits++; }
        if (sw_err_is_missing_binding(all[i])) { hits++; }

        /* 至多命中一类；允许 0 类（持久性失败无专用 helper，取补集） */
        TEST_ASSERT_TRUE_MESSAGE(hits <= 1, sw_err_name(all[i]));
    }
}

/* -------------------------------------------------------------------------
 * 名称查询：每个在用错误码都要有名字，未知值有兜底
 * ------------------------------------------------------------------------- */
static void test_names_present(void)
{
    TEST_ASSERT_EQUAL_STRING("SW_OK", sw_err_name(SW_OK));
    TEST_ASSERT_EQUAL_STRING("SW_ERR_PARAM", sw_err_name(SW_ERR_PARAM));
    TEST_ASSERT_EQUAL_STRING("SW_ERR_NOT_INIT", sw_err_name(SW_ERR_NOT_INIT));
    TEST_ASSERT_EQUAL_STRING("SW_ERR_NOT_FOUND", sw_err_name(SW_ERR_NOT_FOUND));

    /* 废弃的保留值与任意越界值都走兜底，不返回 NULL */
    TEST_ASSERT_EQUAL_STRING("SW_ERR_UNKNOWN", sw_err_name((sw_err_t)-11));
    TEST_ASSERT_EQUAL_STRING("SW_ERR_UNKNOWN", sw_err_name((sw_err_t)-13));
    TEST_ASSERT_EQUAL_STRING("SW_ERR_UNKNOWN", sw_err_name((sw_err_t)-999));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_error_values_are_stable);
    RUN_TEST(test_transient_classification);
    RUN_TEST(test_caller_fault_classification);
    RUN_TEST(test_missing_binding_classification);
    RUN_TEST(test_classifications_are_mutually_exclusive);
    RUN_TEST(test_names_present);
    return UNITY_END();
}
