/**
 * @file    test_machine_ops_port.c
 * @brief   machine_ops_port 机型操作端口单元测试
 *
 * 分组：
 *   A. 注册与获取
 *   B. 回调调用
 */

#include "common/sw_error.h"
#include "ports/outbound/machine/machine_ops_port.h"
#include "unity.h"

#include <stdint.h>

static int s_deferred_stop_count;
static int s_safety_home_count;
static int s_home_device_count;

static void stub_deferred_stop_all(void)
{
    s_deferred_stop_count++;
}

static void stub_safety_home(void)
{
    s_safety_home_count++;
}

static sw_err_t stub_home_device(void)
{
    s_home_device_count++;
    return SW_OK;
}

static const machine_ops_t s_stub_ops = {
    .deferred_stop_all = stub_deferred_stop_all,
    .safety_home       = stub_safety_home,
    .home_device       = stub_home_device,
};

void setUp(void)
{
    s_deferred_stop_count = 0;
    s_safety_home_count   = 0;
    s_home_device_count   = 0;
}

void tearDown(void)
{
}

static void test_get_before_register_returns_null(void)
{
    TEST_ASSERT_NULL(machine_ops_get());
}

static void test_register_and_get(void)
{
    machine_ops_register(&s_stub_ops);
    TEST_ASSERT_EQUAL_PTR(&s_stub_ops, machine_ops_get());
}

static void test_invoke_callbacks(void)
{
    const machine_ops_t *ops;

    machine_ops_register(&s_stub_ops);
    ops = machine_ops_get();
    TEST_ASSERT_NOT_NULL(ops);

    ops->deferred_stop_all();
    ops->safety_home();
    TEST_ASSERT_EQUAL_INT(SW_OK, ops->home_device());

    TEST_ASSERT_EQUAL_INT(1, s_deferred_stop_count);
    TEST_ASSERT_EQUAL_INT(1, s_safety_home_count);
    TEST_ASSERT_EQUAL_INT(1, s_home_device_count);
}

int main(void)
{
    UNITY_BEGIN();

    RUN_TEST(test_get_before_register_returns_null);
    RUN_TEST(test_register_and_get);
    RUN_TEST(test_invoke_callbacks);

    return UNITY_END();
}
