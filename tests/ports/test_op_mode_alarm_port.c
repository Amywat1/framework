/**
 * @file    test_op_mode_alarm_port.c
 * @brief   op_mode_alarm_port 急停码识别默认实现单元测试
 */

#include "ports/outbound/safety/op_mode_alarm_port.h"
#include "unity.h"

#include <stdint.h>

void setUp(void)
{
}

void tearDown(void)
{
}

static void test_default_impl_returns_false(void)
{
    TEST_ASSERT_FALSE(op_mode_alarm_port_is_estop(0U));
    TEST_ASSERT_FALSE(op_mode_alarm_port_is_estop(0xFFFFFFFFU));
}

int main(void)
{
    UNITY_BEGIN();

    RUN_TEST(test_default_impl_returns_false);

    return UNITY_END();
}
