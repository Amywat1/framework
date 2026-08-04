/**
 * @file    test_op_mode_alarm_port.c
 * @brief   安全端口注册表与调用侧包装单元测试
 */

#include "ports/outbound/safety/hw_estop_port.h"
#include "ports/outbound/safety/op_mode_alarm_port.h"
#include "ports/outbound/safety/safety_cutout_port.h"
#include "ports/outbound/safety/safety_deferred_stop.h"
#include "ports/outbound/safety/safety_port.h"
#include "ports/port_registry.h"
#include "unity.h"

#include <stdint.h>

static unsigned s_cutout_calls;
static unsigned s_deferred_calls;
static bool     s_estop_state;
static uint32_t s_last_alarm_code;

static sw_err_t s_cutout_ret = SW_OK;

static sw_err_t fake_cutout(void)
{
    s_cutout_calls++;
    return s_cutout_ret;
}

static bool fake_estop_is_active(void)
{
    return s_estop_state;
}

static bool fake_alarm_is_estop(uint32_t alarm_code)
{
    s_last_alarm_code = alarm_code;
    return alarm_code == 201709U;
}

static void fake_deferred_stop(void)
{
    s_deferred_calls++;
}

static const safety_ops_t s_fake_ops = {
    .cutout          = fake_cutout,
    .estop_is_active = fake_estop_is_active,
    .alarm_is_estop  = fake_alarm_is_estop,
    .deferred_stop   = fake_deferred_stop,
};

void setUp(void)
{
    port_registry_safety_reset();
    s_cutout_calls    = 0U;
    s_deferred_calls  = 0U;
    s_estop_state     = false;
    s_last_alarm_code = 0U;
    s_cutout_ret      = SW_OK;
}

void tearDown(void)
{
    port_registry_safety_reset();
}

/* 未注册时不崩溃，且取故障安全的返回值 */
static void test_unregistered_is_safe(void)
{
    TEST_ASSERT_NULL(safety_port_get_ops());

    /* 未注册返回 SW_ERR_NOT_INIT，与"已执行但失败"区分开 */
    TEST_ASSERT_EQUAL_INT(SW_ERR_NOT_INIT, safety_cutout_execute());
    safety_deferred_stop();
    TEST_ASSERT_FALSE(hw_estop_port_is_active());
    TEST_ASSERT_FALSE(op_mode_alarm_port_is_estop(0U));
    TEST_ASSERT_FALSE(op_mode_alarm_port_is_estop(0xFFFFFFFFU));
}

/* 注册后各入口应委派到实现 */
static void test_registered_delegates(void)
{
    TEST_ASSERT_EQUAL_INT(SW_OK, safety_port_register(&s_fake_ops));

    TEST_ASSERT_EQUAL_INT(SW_OK, safety_cutout_execute());
    TEST_ASSERT_EQUAL_UINT(1U, s_cutout_calls);

    safety_deferred_stop();
    TEST_ASSERT_EQUAL_UINT(1U, s_deferred_calls);

    s_estop_state = true;
    TEST_ASSERT_TRUE(hw_estop_port_is_active());
    s_estop_state = false;
    TEST_ASSERT_FALSE(hw_estop_port_is_active());

    TEST_ASSERT_TRUE(op_mode_alarm_port_is_estop(201709U));
    TEST_ASSERT_EQUAL_UINT32(201709U, s_last_alarm_code);
    TEST_ASSERT_FALSE(op_mode_alarm_port_is_estop(100001U));
}

/* 部分填充的 ops 必须被拒绝，避免某条安全路径静默失效 */
static void test_partial_ops_rejected(void)
{
    static const safety_ops_t s_missing_cutout = {
        .cutout          = NULL,
        .estop_is_active = fake_estop_is_active,
        .alarm_is_estop  = fake_alarm_is_estop,
        .deferred_stop   = fake_deferred_stop,
    };
    static const safety_ops_t s_missing_deferred = {
        .cutout          = fake_cutout,
        .estop_is_active = fake_estop_is_active,
        .alarm_is_estop  = fake_alarm_is_estop,
        .deferred_stop   = NULL,
    };

    TEST_ASSERT_EQUAL_INT(SW_ERR_PARAM, safety_port_register(&s_missing_cutout));
    TEST_ASSERT_NULL(safety_port_get_ops());

    TEST_ASSERT_EQUAL_INT(SW_ERR_PARAM, safety_port_register(&s_missing_deferred));
    TEST_ASSERT_NULL(safety_port_get_ops());
}

/* 拒绝非法注册时不得破坏已有的合法注册 */
static void test_rejected_registration_keeps_previous(void)
{
    static const safety_ops_t s_bad = {
        .cutout          = NULL,
        .estop_is_active = NULL,
        .alarm_is_estop  = NULL,
        .deferred_stop   = NULL,
    };

    TEST_ASSERT_EQUAL_INT(SW_OK, safety_port_register(&s_fake_ops));
    TEST_ASSERT_EQUAL_INT(SW_ERR_PARAM, safety_port_register(&s_bad));

    TEST_ASSERT_EQUAL_PTR(&s_fake_ops, safety_port_get_ops());
    TEST_ASSERT_EQUAL_INT(SW_OK, safety_cutout_execute());
    TEST_ASSERT_EQUAL_UINT(1U, s_cutout_calls);
}

/* 传 NULL 为显式解除注册，返回成功 */
static void test_null_unregisters(void)
{
    TEST_ASSERT_EQUAL_INT(SW_OK, safety_port_register(&s_fake_ops));
    TEST_ASSERT_EQUAL_INT(SW_OK, safety_port_register(NULL));
    TEST_ASSERT_NULL(safety_port_get_ops());

    /* 解除后回到故障安全行为 */
    TEST_ASSERT_EQUAL_INT(SW_ERR_NOT_INIT, safety_cutout_execute());
    TEST_ASSERT_EQUAL_UINT(0U, s_cutout_calls);
}

/* 切断失败必须原样上传给调用方，并被计数 */
static void test_cutout_failure_reported_and_counted(void)
{
    TEST_ASSERT_EQUAL_INT(SW_OK, safety_port_register(&s_fake_ops));
    TEST_ASSERT_EQUAL_UINT(0U, safety_cutout_failure_count());

    s_cutout_ret = SW_ERR_HW;
    TEST_ASSERT_EQUAL_INT(SW_ERR_HW, safety_cutout_execute());
    TEST_ASSERT_EQUAL_UINT(1U, s_cutout_calls);
    TEST_ASSERT_EQUAL_UINT(1U, safety_cutout_failure_count());

    /* 连续失败逐次累加，不做首次节流：每一条都是现场判因证据 */
    TEST_ASSERT_EQUAL_INT(SW_ERR_HW, safety_cutout_execute());
    TEST_ASSERT_EQUAL_UINT(2U, safety_cutout_failure_count());

    /* 恢复成功后不再累加，计数只反映失败 */
    s_cutout_ret = SW_OK;
    TEST_ASSERT_EQUAL_INT(SW_OK, safety_cutout_execute());
    TEST_ASSERT_EQUAL_UINT(2U, safety_cutout_failure_count());
}

/* 未注册导致的"未执行"不计入切断失败计数，两者语义不同 */
static void test_unregistered_not_counted_as_failure(void)
{
    TEST_ASSERT_EQUAL_INT(SW_ERR_NOT_INIT, safety_cutout_execute());
    TEST_ASSERT_EQUAL_UINT(0U, safety_cutout_failure_count());
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_unregistered_is_safe);
    RUN_TEST(test_registered_delegates);
    RUN_TEST(test_partial_ops_rejected);
    RUN_TEST(test_rejected_registration_keeps_previous);
    RUN_TEST(test_null_unregisters);
    RUN_TEST(test_cutout_failure_reported_and_counted);
    RUN_TEST(test_unregistered_not_counted_as_failure);
    return UNITY_END();
}
