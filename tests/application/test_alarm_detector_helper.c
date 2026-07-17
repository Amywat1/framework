/**
 * @file    test_alarm_detector_helper.c
 * @brief   alarm_detector_helper 单元测试
 */

#include "adapters/inbound/safety/alarm_detector_helper.h"
#include "common/sw_error.h"
#include "ports/inbound/safety/alarm_binding_port.h"
#include "unity.h"

static unsigned g_trigger_count;
static unsigned g_clear_count;
static uint32_t g_last_trigger;
static uint32_t g_last_clear;
static sw_err_t g_trigger_ret;
static sw_err_t g_clear_ret;

static sw_err_t fake_trigger(uint32_t alarm_code)
{
    g_trigger_count++;
    g_last_trigger = alarm_code;
    return g_trigger_ret;
}

static sw_err_t fake_clear(uint32_t alarm_code)
{
    g_clear_count++;
    g_last_clear = alarm_code;
    return g_clear_ret;
}

void setUp(void)
{
    static const alarm_binding_ops_t ops = {
        .trigger      = fake_trigger,
        .clear        = fake_clear,
        .load_catalog = NULL,
    };

    g_trigger_count = 0U;
    g_clear_count   = 0U;
    g_last_trigger  = 0U;
    g_last_clear    = 0U;
    g_trigger_ret   = SW_OK;
    g_clear_ret     = SW_OK;
    alarm_binding_register(&ops);
}

void tearDown(void)
{
    alarm_binding_register(NULL);
}

static void test_detector_triggers_and_clears_on_edges(void)
{
    alarm_detector_t           detector;
    const alarm_detector_cfg_t cfg = {
        .alarm_code          = 201101U,
        .clear_when_inactive = true,
    };

    TEST_ASSERT_EQUAL_INT(SW_OK, alarm_detector_init(&detector, &cfg));
    TEST_ASSERT_EQUAL_INT(SW_OK, alarm_detector_update(&detector, false));
    TEST_ASSERT_EQUAL_UINT(0U, g_trigger_count);

    TEST_ASSERT_EQUAL_INT(SW_OK, alarm_detector_update(&detector, true));
    TEST_ASSERT_TRUE(alarm_detector_is_active(&detector));
    TEST_ASSERT_EQUAL_UINT(1U, g_trigger_count);
    TEST_ASSERT_EQUAL_UINT32(201101U, g_last_trigger);

    TEST_ASSERT_EQUAL_INT(SW_OK, alarm_detector_update(&detector, true));
    TEST_ASSERT_EQUAL_UINT(1U, g_trigger_count);

    TEST_ASSERT_EQUAL_INT(SW_OK, alarm_detector_update(&detector, false));
    TEST_ASSERT_FALSE(alarm_detector_is_active(&detector));
    TEST_ASSERT_EQUAL_UINT(1U, g_clear_count);
    TEST_ASSERT_EQUAL_UINT32(201101U, g_last_clear);
}

static void test_detector_can_hold_until_manual_clear(void)
{
    alarm_detector_t           detector;
    const alarm_detector_cfg_t cfg = {
        .alarm_code          = 201102U,
        .clear_when_inactive = false,
    };

    TEST_ASSERT_EQUAL_INT(SW_OK, alarm_detector_init(&detector, &cfg));
    TEST_ASSERT_EQUAL_INT(SW_OK, alarm_detector_update(&detector, true));
    TEST_ASSERT_EQUAL_INT(SW_OK, alarm_detector_update(&detector, false));

    TEST_ASSERT_FALSE(alarm_detector_is_active(&detector));
    TEST_ASSERT_EQUAL_UINT(1U, g_trigger_count);
    TEST_ASSERT_EQUAL_UINT(0U, g_clear_count);
}

static void test_detector_keeps_state_when_binding_fails(void)
{
    alarm_detector_t           detector;
    const alarm_detector_cfg_t cfg = {
        .alarm_code          = 201103U,
        .clear_when_inactive = true,
    };

    TEST_ASSERT_EQUAL_INT(SW_OK, alarm_detector_init(&detector, &cfg));

    g_trigger_ret = SW_ERR_HW;
    TEST_ASSERT_EQUAL_INT(SW_ERR_HW, alarm_detector_update(&detector, true));
    TEST_ASSERT_FALSE(alarm_detector_is_active(&detector));

    g_trigger_ret = SW_OK;
    TEST_ASSERT_EQUAL_INT(SW_OK, alarm_detector_update(&detector, true));
    TEST_ASSERT_TRUE(alarm_detector_is_active(&detector));

    g_clear_ret = SW_ERR_HW;
    TEST_ASSERT_EQUAL_INT(SW_ERR_HW, alarm_detector_update(&detector, false));
    TEST_ASSERT_TRUE(alarm_detector_is_active(&detector));
}

static void test_detector_rejects_invalid_config_and_unregistered_port(void)
{
    alarm_detector_t           detector;
    const alarm_detector_cfg_t bad_cfg = {
        .alarm_code          = 0U,
        .clear_when_inactive = true,
    };
    const alarm_detector_cfg_t cfg = {
        .alarm_code          = 201104U,
        .clear_when_inactive = true,
    };

    TEST_ASSERT_EQUAL_INT(SW_ERR_PARAM, alarm_detector_init(NULL, &cfg));
    TEST_ASSERT_EQUAL_INT(SW_ERR_PARAM, alarm_detector_init(&detector, NULL));
    TEST_ASSERT_EQUAL_INT(SW_ERR_PARAM, alarm_detector_init(&detector, &bad_cfg));

    TEST_ASSERT_EQUAL_INT(SW_OK, alarm_detector_init(&detector, &cfg));
    alarm_binding_register(NULL);
    TEST_ASSERT_EQUAL_INT(SW_ERR_NOT_INIT, alarm_detector_update(&detector, true));
}

int main(void)
{
    UNITY_BEGIN();

    RUN_TEST(test_detector_triggers_and_clears_on_edges);
    RUN_TEST(test_detector_can_hold_until_manual_clear);
    RUN_TEST(test_detector_keeps_state_when_binding_fails);
    RUN_TEST(test_detector_rejects_invalid_config_and_unregistered_port);

    return UNITY_END();
}
