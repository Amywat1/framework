/**
 * @file    test_hal_sensor_filter.c
 * @brief   hal_sensor_filter 单元测试
 */

#include "adapters/outbound/hal/components/sensor_filter/hal_sensor_filter.h"
#include "adapters/outbound/hal/sim/hal_io_sim.h"
#include "common/io_handle.h"
#include "common/sw_error.h"
#include "ports/outbound/hal/hal_io_port.h"
#include "ports/outbound/hal/hal_sensor_port.h"
#include "unity.h"

#define TEST_DI io_di_make(1U, 1U)

static hal_sensor_bind_cfg_t make_cfg(void)
{
    hal_sensor_bind_cfg_t cfg = {
        .pin           = TEST_DI,
        .active_low    = false,
        .trig_count    = 2U,
        .release_count = 3U,
    };

    return cfg;
}

void setUp(void)
{
    hal_io_sim_register();
    hal_sensor_filter_register();
    TEST_ASSERT_NOT_NULL(hal_sensor_get_ops());
    TEST_ASSERT_EQUAL_INT(SW_OK, hal_sensor_get_ops()->init());
    hal_io_sim_set_di_level(TEST_DI, false);
}

void tearDown(void)
{
}

static void test_bind_rejects_invalid_params(void)
{
    hal_sensor_bind_cfg_t cfg = make_cfg();

    TEST_ASSERT_EQUAL_INT(SW_ERR_PARAM, hal_sensor_filter_bind(HAL_SENSOR_CHANNEL_MAX, &cfg));
    TEST_ASSERT_EQUAL_INT(SW_ERR_PARAM, hal_sensor_filter_bind(0U, NULL));

    cfg.pin = (io_di_t){0U};
    TEST_ASSERT_EQUAL_INT(SW_ERR_PARAM, hal_sensor_filter_bind(0U, &cfg));

    cfg            = make_cfg();
    cfg.trig_count = 0U;
    TEST_ASSERT_EQUAL_INT(SW_ERR_PARAM, hal_sensor_filter_bind(0U, &cfg));

    cfg               = make_cfg();
    cfg.release_count = 0U;
    TEST_ASSERT_EQUAL_INT(SW_ERR_PARAM, hal_sensor_filter_bind(0U, &cfg));
}

static void test_warmup_confirms_active_after_threshold(void)
{
    hal_sensor_bind_cfg_t cfg = make_cfg();

    TEST_ASSERT_EQUAL_INT(SW_OK, hal_sensor_filter_bind(0U, &cfg));
    TEST_ASSERT_FALSE(hal_sensor_get_ops()->is_active(0U));

    hal_io_sim_set_di_level(TEST_DI, true);
    TEST_ASSERT_EQUAL_INT(SW_OK, hal_sensor_get_ops()->warmup(1U));
    TEST_ASSERT_FALSE(hal_sensor_get_ops()->is_active(0U));

    TEST_ASSERT_EQUAL_INT(SW_OK, hal_sensor_get_ops()->warmup(1U));
    TEST_ASSERT_TRUE(hal_sensor_get_ops()->is_active(0U));
}

static void test_release_uses_release_threshold(void)
{
    hal_sensor_bind_cfg_t cfg = make_cfg();

    TEST_ASSERT_EQUAL_INT(SW_OK, hal_sensor_filter_bind(0U, &cfg));
    hal_io_sim_set_di_level(TEST_DI, true);
    TEST_ASSERT_EQUAL_INT(SW_OK, hal_sensor_get_ops()->warmup(2U));
    TEST_ASSERT_TRUE(hal_sensor_get_ops()->is_active(0U));

    hal_io_sim_set_di_level(TEST_DI, false);
    TEST_ASSERT_EQUAL_INT(SW_OK, hal_sensor_get_ops()->warmup(2U));
    TEST_ASSERT_TRUE(hal_sensor_get_ops()->is_active(0U));

    TEST_ASSERT_EQUAL_INT(SW_OK, hal_sensor_get_ops()->warmup(1U));
    TEST_ASSERT_FALSE(hal_sensor_get_ops()->is_active(0U));
}

static void test_active_low_inverts_raw_level(void)
{
    hal_sensor_bind_cfg_t cfg = make_cfg();

    cfg.active_low = true;
    TEST_ASSERT_EQUAL_INT(SW_OK, hal_sensor_filter_bind(1U, &cfg));
    hal_io_sim_set_di_level(TEST_DI, false);
    TEST_ASSERT_EQUAL_INT(SW_OK, hal_sensor_get_ops()->warmup(2U));
    TEST_ASSERT_TRUE(hal_sensor_get_ops()->is_active(1U));

    hal_io_sim_set_di_level(TEST_DI, true);
    TEST_ASSERT_EQUAL_INT(SW_OK, hal_sensor_get_ops()->warmup(3U));
    TEST_ASSERT_FALSE(hal_sensor_get_ops()->is_active(1U));
}

static void test_init_resets_runtime_but_keeps_bindings(void)
{
    hal_sensor_bind_cfg_t cfg = make_cfg();

    TEST_ASSERT_EQUAL_INT(SW_OK, hal_sensor_filter_bind(0U, &cfg));
    hal_io_sim_set_di_level(TEST_DI, true);
    TEST_ASSERT_EQUAL_INT(SW_OK, hal_sensor_get_ops()->warmup(2U));
    TEST_ASSERT_TRUE(hal_sensor_get_ops()->is_active(0U));

    TEST_ASSERT_EQUAL_INT(SW_OK, hal_sensor_get_ops()->init());
    TEST_ASSERT_FALSE(hal_sensor_get_ops()->is_active(0U));
    TEST_ASSERT_EQUAL_INT(SW_OK, hal_sensor_get_ops()->warmup(2U));
    TEST_ASSERT_TRUE(hal_sensor_get_ops()->is_active(0U));
}

static void test_warmup_requires_registered_io_ops(void)
{
    hal_sensor_bind_cfg_t cfg = make_cfg();

    TEST_ASSERT_EQUAL_INT(SW_OK, hal_sensor_filter_bind(0U, &cfg));
    hal_io_register(NULL);

    TEST_ASSERT_EQUAL_INT(SW_ERR_NOT_INIT, hal_sensor_get_ops()->warmup(1U));
}

int main(void)
{
    UNITY_BEGIN();

    RUN_TEST(test_bind_rejects_invalid_params);
    RUN_TEST(test_warmup_confirms_active_after_threshold);
    RUN_TEST(test_release_uses_release_threshold);
    RUN_TEST(test_active_low_inverts_raw_level);
    RUN_TEST(test_init_resets_runtime_but_keeps_bindings);
    RUN_TEST(test_warmup_requires_registered_io_ops);

    return UNITY_END();
}
