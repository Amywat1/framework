/**
 * @file    test_hal_do_group.c
 * @brief   hal_do_group DO 组×槽位 HAL 单元测试
 *
 * 分组：
 *   A. 绑定参数校验
 *   B. 槽位输出控制
 *   C. 全部关闭
 *   D. 边界与异常
 */

#include "framework/adapters/outbound/hal/components/do_group_mapper/hal_do_group_mapper.h"
#include "framework/ports/outbound/hal/hal_do_group_port.h"
#include "framework/ports/outbound/hal/hal_io_port.h"
#include "framework/common/io_handle.h"
#include "framework/common/sw_error.h"
#include "unity.h"

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#define TEST_BOARD  1U

/* -------------------------------------------------------------------------
 * Mock IO（只实现 do_set）
 * pin 编号用作数组索引
 * ------------------------------------------------------------------------- */
static bool     s_do_state[8];
static sw_err_t s_do_set_ret = SW_OK;

static sw_err_t mock_do_set(io_do_t pin, bool val)
{
    uint16_t p = io_handle_pin(io_do_raw(pin));
    if (p < 8U) { s_do_state[p] = val; }
    return s_do_set_ret;
}

static const hal_io_ops_t s_mock_io_ops = {
    .do_set = mock_do_set,
};

void setUp(void)
{
    memset(s_do_state, 0, sizeof(s_do_state));
    s_do_set_ret = SW_OK;

    hal_io_register(&s_mock_io_ops);
    hal_do_group_mapper_register();
    hal_do_group_get_ops()->init();
}

void tearDown(void) {}

/* =========================================================================
 * A. 绑定参数校验
 * ========================================================================= */

static void test_bind_invalid_group(void)
{
    TEST_ASSERT_EQUAL_INT(SW_ERR_PARAM,
        hal_do_group_mapper_bind(HAL_DO_GROUP_MAX, 0U, IO_DO(TEST_BOARD, 1U)));
}

static void test_bind_invalid_slot(void)
{
    TEST_ASSERT_EQUAL_INT(SW_ERR_PARAM,
        hal_do_group_mapper_bind(0U, HAL_DO_SLOT_MAX, IO_DO(TEST_BOARD, 1U)));
}

static void test_bind_null_pin_marks_unbound(void)
{
    const hal_do_group_ops_t *ops = hal_do_group_get_ops();

    /* IO_HANDLE_NULL 绑定成功，但标记为未安装 */
    TEST_ASSERT_EQUAL_INT(SW_OK,
        hal_do_group_mapper_bind(0U, 0U, (io_do_t){IO_HANDLE_NULL}));
    /* 未安装槽位 slot_set 应返回 SW_ERR_NOT_INIT */
    TEST_ASSERT_EQUAL_INT(SW_ERR_NOT_INIT,
        ops->slot_set(0U, 0U, true));
}

static void test_bind_valid(void)
{
    TEST_ASSERT_EQUAL_INT(SW_OK,
        hal_do_group_mapper_bind(0U, 0U, IO_DO(TEST_BOARD, 1U)));
}

/* =========================================================================
 * B. 槽位输出控制
 * ========================================================================= */

static void test_slot_set_on(void)
{
    const hal_do_group_ops_t *ops = hal_do_group_get_ops();

    hal_do_group_mapper_bind(0U, 0U, IO_DO(TEST_BOARD, 1U));
    TEST_ASSERT_EQUAL_INT(SW_OK,
        ops->slot_set(0U, 0U, true));
    TEST_ASSERT_TRUE(s_do_state[1]);
}

static void test_slot_set_off(void)
{
    const hal_do_group_ops_t *ops = hal_do_group_get_ops();

    hal_do_group_mapper_bind(0U, 0U, IO_DO(TEST_BOARD, 2U));
    ops->slot_set(0U, 0U, true);
    TEST_ASSERT_EQUAL_INT(SW_OK,
        ops->slot_set(0U, 0U, false));
    TEST_ASSERT_FALSE(s_do_state[2]);
}

static void test_slot_set_unbound_returns_not_init(void)
{
    /* 槽位 (0, 3) 从不绑定任何有效引脚 */
    TEST_ASSERT_EQUAL_INT(SW_ERR_NOT_INIT,
        hal_do_group_get_ops()->slot_set(0U, 3U, true));
}

static void test_slot_set_invalid_group_returns_not_init(void)
{
    TEST_ASSERT_EQUAL_INT(SW_ERR_NOT_INIT,
        hal_do_group_get_ops()->slot_set(HAL_DO_GROUP_MAX, 0U, true));
}

/* =========================================================================
 * C. 全部关闭
 * ========================================================================= */

static void test_all_off_clears_all_bound(void)
{
    const hal_do_group_ops_t *ops = hal_do_group_get_ops();
    hal_do_group_mapper_bind(0U, 0U, IO_DO(TEST_BOARD, 1U));
    hal_do_group_mapper_bind(0U, 1U, IO_DO(TEST_BOARD, 2U));

    ops->slot_set(0U, 0U, true);
    ops->slot_set(0U, 1U, true);
    TEST_ASSERT_TRUE(s_do_state[1]);
    TEST_ASSERT_TRUE(s_do_state[2]);

    TEST_ASSERT_EQUAL_INT(SW_OK, ops->all_off());
    TEST_ASSERT_FALSE(s_do_state[1]);
    TEST_ASSERT_FALSE(s_do_state[2]);
}

/* =========================================================================
 * D. 补充场景
 * ========================================================================= */

static void test_max_valid_group_and_slot(void)
{
    const hal_do_group_ops_t *ops = hal_do_group_get_ops();

    /* 最大合法下标 group=HAL_DO_GROUP_MAX-1, slot=HAL_DO_SLOT_MAX-1 */
    hal_do_group_t g = (hal_do_group_t)(HAL_DO_GROUP_MAX - 1U);
    hal_do_slot_t  s = (hal_do_slot_t)(HAL_DO_SLOT_MAX  - 1U);
    TEST_ASSERT_EQUAL_INT(SW_OK,
        hal_do_group_mapper_bind(g, s, IO_DO(TEST_BOARD, 7U)));
    TEST_ASSERT_EQUAL_INT(SW_OK,
        ops->slot_set(g, s, true));
    TEST_ASSERT_TRUE(s_do_state[7]);
}

static void test_rebind_slot_changes_pin(void)
{
    const hal_do_group_ops_t *ops = hal_do_group_get_ops();

    hal_do_group_mapper_bind(0U, 0U, IO_DO(TEST_BOARD, 1U));
    ops->slot_set(0U, 0U, false); /* 确保 pin1 为 false */

    hal_do_group_mapper_bind(0U, 0U, IO_DO(TEST_BOARD, 2U)); /* 改绑 pin2 */
    ops->slot_set(0U, 0U, true);

    TEST_ASSERT_FALSE(s_do_state[1]); /* pin1 未被改变 */
    TEST_ASSERT_TRUE(s_do_state[2]);  /* pin2 被置为 true */
}

static void test_slot_set_no_io_ops_returns_err(void)
{
    const hal_do_group_ops_t *ops = hal_do_group_get_ops();

    hal_do_group_mapper_bind(0U, 0U, IO_DO(TEST_BOARD, 1U));
    hal_io_register(NULL); /* 移除 IO 后端 */
    TEST_ASSERT_EQUAL_INT(SW_ERR_NOT_INIT,
        ops->slot_set(0U, 0U, true));
}

static void test_all_off_no_io_ops_returns_err(void)
{
    const hal_do_group_ops_t *ops = hal_do_group_get_ops();

    hal_do_group_mapper_bind(0U, 0U, IO_DO(TEST_BOARD, 1U));
    hal_io_register(NULL);
    /* all_off 遍历所有绑定槽位并记录首个错误 */
    TEST_ASSERT_EQUAL_INT(SW_ERR_NOT_INIT,
        ops->all_off());
}

int main(void)
{
    UNITY_BEGIN();

    RUN_TEST(test_bind_invalid_group);
    RUN_TEST(test_bind_invalid_slot);
    RUN_TEST(test_bind_null_pin_marks_unbound);
    RUN_TEST(test_bind_valid);

    RUN_TEST(test_slot_set_on);
    RUN_TEST(test_slot_set_off);
    RUN_TEST(test_slot_set_unbound_returns_not_init);
    RUN_TEST(test_slot_set_invalid_group_returns_not_init);

    RUN_TEST(test_all_off_clears_all_bound);

    RUN_TEST(test_max_valid_group_and_slot);
    RUN_TEST(test_rebind_slot_changes_pin);
    RUN_TEST(test_slot_set_no_io_ops_returns_err);
    RUN_TEST(test_all_off_no_io_ops_returns_err);

    return UNITY_END();
}
