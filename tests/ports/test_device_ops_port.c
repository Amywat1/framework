/**
 * @file    test_device_ops_port.c
 * @brief   device_ops_port 机型操作端口单元测试
 */

#include "common/sw_error.h"
#include "domain/ports/outbound/device/device_ops_port.h"
#include "wdf_test_spec.h"

#include <stdint.h>

static int s_abort_home_count;
static int s_home_device_count;

static void stub_abort_home(void)
{
    s_abort_home_count++;
}

static sw_err_t stub_home_device(void)
{
    s_home_device_count++;
    return SW_OK;
}

static const device_ops_t s_stub_ops = {
    .abort_home  = stub_abort_home,
    .home_device = stub_home_device,
};

void setUp(void)
{
    s_abort_home_count  = 0;
    s_home_device_count = 0;
}

void tearDown(void)
{
}

static void test_get_before_register_returns_null(void)
{
    TEST_ASSERT_NULL(device_ops_get());
}

static void test_register_and_get(void)
{
    device_ops_register(&s_stub_ops);
    TEST_ASSERT_EQUAL_PTR(&s_stub_ops, device_ops_get());
}

static void test_invoke_callbacks(void)
{
    const device_ops_t *ops;

    device_ops_register(&s_stub_ops);
    ops = device_ops_get();
    TEST_ASSERT_NOT_NULL(ops);

    ops->abort_home();
    TEST_ASSERT_EQUAL_INT(SW_OK, ops->home_device());

    TEST_ASSERT_EQUAL_INT(1, s_abort_home_count);
    TEST_ASSERT_EQUAL_INT(1, s_home_device_count);
}

int main(void)
{
    UNITY_BEGIN();

    WDF_RUN_TEST(test_get_before_register_returns_null, "", "验证端口注册前获取返回空指针");
    WDF_RUN_TEST(test_register_and_get, "", "验证注册并获取");
    WDF_RUN_TEST(test_invoke_callbacks, "", "验证调用回调");

    return UNITY_END();
}
