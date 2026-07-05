/**
 * @file    test_hal_sensor.c
 * @brief   hal_sensor 通用 DI 滤波单元测试
 *
 * 分组：
 *   A. 绑定参数校验
 *   B. 防抖计数逻辑
 *   C. active_low 极性
 *   D. 边界与异常
 */

#include "framework/adapters/outbound/hal/components/sensor_filter/hal_sensor_filter.h"
#include "framework/ports/outbound/hal/hal_sensor_port.h"
#include "framework/ports/outbound/hal/hal_io_port.h"
#include "framework/common/io_handle.h"
#include "framework/common/sw_error.h"
#include "unity.h"

#include <stdbool.h>
#include <stdint.h>

/* -------------------------------------------------------------------------
 * Mock IO（只实现 di_read）
 * ------------------------------------------------------------------------- */
static bool s_mock_di = false;

static bool mock_di_read(io_di_t pin)
{
    (void)pin;
    return s_mock_di;
}

static const hal_io_ops_t s_mock_io_ops = {
    .di_read = mock_di_read,
};

/* 测试用 DI 句柄 */
static const io_di_t k_pin = IO_DI(1U, 1U);

void setUp(void)
{
    s_mock_di = false;
    hal_io_register(&s_mock_io_ops);
    hal_sensor_filter_register();
    hal_sensor_get_ops()->init();
}

void tearDown(void) {}

/* =========================================================================
 * A. 绑定参数校验
 * ========================================================================= */

static void test_bind_null_cfg(void)
{
    TEST_ASSERT_EQUAL_INT(SW_ERR_PARAM, hal_sensor_get_ops()->bind(0U, NULL));
}

static void test_bind_null_pin(void)
{
    hal_sensor_bind_cfg_t cfg = {
        .pin           = (io_di_t){IO_HANDLE_NULL},
        .trig_count    = 1U,
        .release_count = 1U,
    };
    TEST_ASSERT_EQUAL_INT(SW_ERR_PARAM, hal_sensor_get_ops()->bind(0U, &cfg));
}

static void test_bind_zero_trig_count(void)
{
    hal_sensor_bind_cfg_t cfg = {
        .pin           = k_pin,
        .trig_count    = 0U,
        .release_count = 1U,
    };
    TEST_ASSERT_EQUAL_INT(SW_ERR_PARAM, hal_sensor_get_ops()->bind(0U, &cfg));
}

static void test_bind_zero_release_count(void)
{
    hal_sensor_bind_cfg_t cfg = {
        .pin           = k_pin,
        .trig_count    = 1U,
        .release_count = 0U,
    };
    TEST_ASSERT_EQUAL_INT(SW_ERR_PARAM, hal_sensor_get_ops()->bind(0U, &cfg));
}

static void test_bind_valid(void)
{
    hal_sensor_bind_cfg_t cfg = {
        .pin           = k_pin,
        .trig_count    = 3U,
        .release_count = 2U,
    };
    TEST_ASSERT_EQUAL_INT(SW_OK, hal_sensor_get_ops()->bind(0U, &cfg));
}

/* =========================================================================
 * B. 防抖计数逻辑
 * ========================================================================= */

static void test_not_active_before_trig_count(void)
{
    hal_sensor_bind_cfg_t cfg = {
        .pin = k_pin, .trig_count = 3U, .release_count = 1U,
    };
    const hal_sensor_ops_t *ops = hal_sensor_get_ops();
    ops->bind(0U, &cfg);

    s_mock_di = true;
    (void)ops->warmup(1U); /* stable_count = 1 */
    (void)ops->warmup(1U); /* stable_count = 2 */
    TEST_ASSERT_FALSE(ops->is_active(0U));
}

static void test_active_after_trig_count(void)
{
    hal_sensor_bind_cfg_t cfg = {
        .pin = k_pin, .trig_count = 3U, .release_count = 1U,
    };
    const hal_sensor_ops_t *ops = hal_sensor_get_ops();
    ops->bind(0U, &cfg);

    s_mock_di = true;
    (void)ops->warmup(1U); (void)ops->warmup(1U); (void)ops->warmup(1U);
    TEST_ASSERT_TRUE(ops->is_active(0U));
}

static void test_stays_active_before_release_count(void)
{
    hal_sensor_bind_cfg_t cfg = {
        .pin = k_pin, .trig_count = 1U, .release_count = 3U,
    };
    const hal_sensor_ops_t *ops = hal_sensor_get_ops();
    ops->bind(0U, &cfg);

    s_mock_di = true;
    (void)ops->warmup(1U);
    TEST_ASSERT_TRUE(ops->is_active(0U));

    s_mock_di = false;
    (void)ops->warmup(1U); /* stable_count=1，未满 release_count=3 */
    (void)ops->warmup(1U); /* stable_count=2 */
    TEST_ASSERT_TRUE(ops->is_active(0U)); /* 仍激活 */
}

static void test_released_after_release_count(void)
{
    hal_sensor_bind_cfg_t cfg = {
        .pin = k_pin, .trig_count = 1U, .release_count = 3U,
    };
    const hal_sensor_ops_t *ops = hal_sensor_get_ops();
    ops->bind(0U, &cfg);

    s_mock_di = true;
    (void)ops->warmup(1U);
    TEST_ASSERT_TRUE(ops->is_active(0U));

    s_mock_di = false;
    (void)ops->warmup(1U); (void)ops->warmup(1U); (void)ops->warmup(1U);
    TEST_ASSERT_FALSE(ops->is_active(0U));
}

static void test_interrupt_trig_resets_count(void)
{
    hal_sensor_bind_cfg_t cfg = {
        .pin = k_pin, .trig_count = 3U, .release_count = 1U,
    };
    const hal_sensor_ops_t *ops = hal_sensor_get_ops();
    ops->bind(0U, &cfg);

    /* 触发两次后中途变化，计数归 1 */
    s_mock_di = true;
    (void)ops->warmup(1U); (void)ops->warmup(1U);
    s_mock_di = false;
    (void)ops->warmup(1U);           /* stable_count 从 1（release 计数）开始 */
    s_mock_di = true;
    (void)ops->warmup(1U); (void)ops->warmup(1U); /* 重新开始，stable_count=1,2 */
    TEST_ASSERT_FALSE(ops->is_active(0U));

    (void)ops->warmup(1U); /* stable_count=3 → 激活 */
    TEST_ASSERT_TRUE(ops->is_active(0U));
}

/* =========================================================================
 * C. active_low 极性
 * ========================================================================= */

static void test_active_low_low_level_is_active(void)
{
    hal_sensor_bind_cfg_t cfg = {
        .pin        = k_pin,
        .active_low = true,
        .trig_count = 1U, .release_count = 1U,
    };
    const hal_sensor_ops_t *ops = hal_sensor_get_ops();
    ops->bind(0U, &cfg);

    s_mock_di = false; /* 低电平 = 有效 */
    (void)ops->warmup(1U);
    TEST_ASSERT_TRUE(ops->is_active(0U));
}

static void test_active_low_high_level_is_inactive(void)
{
    hal_sensor_bind_cfg_t cfg = {
        .pin        = k_pin,
        .active_low = true,
        .trig_count = 1U, .release_count = 1U,
    };
    const hal_sensor_ops_t *ops = hal_sensor_get_ops();
    ops->bind(0U, &cfg);

    s_mock_di = false;
    (void)ops->warmup(1U);
    TEST_ASSERT_TRUE(ops->is_active(0U));

    s_mock_di = true; /* 高电平 = 无效 */
    (void)ops->warmup(1U);
    TEST_ASSERT_FALSE(ops->is_active(0U));
}

/* =========================================================================
 * D. 边界与异常
 * ========================================================================= */

static void test_unbound_channel_returns_false(void)
{
    /* 通道 10 从未绑定 */
    TEST_ASSERT_FALSE(hal_sensor_get_ops()->is_active(10U));
}

static void test_init_resets_runtime_state(void)
{
    hal_sensor_bind_cfg_t cfg = {
        .pin = k_pin, .trig_count = 1U, .release_count = 1U,
    };
    const hal_sensor_ops_t *ops = hal_sensor_get_ops();
    ops->bind(0U, &cfg);

    s_mock_di = true;
    (void)ops->warmup(1U); /* 已激活 */
    TEST_ASSERT_TRUE(ops->is_active(0U));

    ops->init(); /* 重置运行时，不清除绑定 */
    TEST_ASSERT_FALSE(ops->is_active(0U));
}

static void test_no_io_ops_warmup_does_not_crash(void)
{
    hal_sensor_bind_cfg_t cfg = {
        .pin = k_pin, .trig_count = 1U, .release_count = 1U,
    };
    hal_sensor_get_ops()->bind(0U, &cfg);
    hal_io_register(NULL); /* 移除 IO 后端 */

    TEST_ASSERT_EQUAL_INT(SW_ERR_NOT_INIT, hal_sensor_get_ops()->warmup(1U));
    TEST_ASSERT_FALSE(hal_sensor_get_ops()->is_active(0U));
}

/* =========================================================================
 * E. 补充场景
 * ========================================================================= */

static void test_trig_count_1_single_sample_activates(void)
{
    hal_sensor_bind_cfg_t cfg = {
        .pin = k_pin, .trig_count = 1U, .release_count = 1U,
    };
    const hal_sensor_ops_t *ops = hal_sensor_get_ops();
    ops->bind(0U, &cfg);

    s_mock_di = true;
    (void)ops->warmup(1U);
    TEST_ASSERT_TRUE(ops->is_active(0U));
}

static void test_multiple_channels_are_independent(void)
{
    /* ch0 trig=1, ch1 trig=3，同一 di 信号只有 ch0 能即时激活 */
    hal_sensor_bind_cfg_t cfg0 = { .pin = k_pin, .trig_count = 1U, .release_count = 1U };
    hal_sensor_bind_cfg_t cfg1 = { .pin = k_pin, .trig_count = 3U, .release_count = 1U };
    const hal_sensor_ops_t *ops = hal_sensor_get_ops();
    ops->bind(0U, &cfg0);
    ops->bind(1U, &cfg1);

    s_mock_di = true;
    (void)ops->warmup(1U);
    TEST_ASSERT_TRUE(ops->is_active(0U));
    TEST_ASSERT_FALSE(ops->is_active(1U)); /* ch1 stable_count=1 < threshold=3 */
}

static void test_rebind_overwrites_config(void)
{
    /* 先绑定 trig=3，再重新绑定 trig=1，后者生效 */
    hal_sensor_bind_cfg_t cfg = { .pin = k_pin, .trig_count = 3U, .release_count = 1U };
    const hal_sensor_ops_t *ops = hal_sensor_get_ops();
    ops->bind(0U, &cfg);

    cfg.trig_count = 1U;
    ops->bind(0U, &cfg); /* 覆盖 */

    s_mock_di = true;
    (void)ops->warmup(1U); /* 新 trig=1，只需一次即激活 */
    TEST_ASSERT_TRUE(ops->is_active(0U));
}

static void test_active_low_with_debounce(void)
{
    /* active_low + trig_count=3：低电平需稳定 3 次才激活 */
    hal_sensor_bind_cfg_t cfg = {
        .pin        = k_pin,
        .active_low = true,
        .trig_count = 3U, .release_count = 1U,
    };
    const hal_sensor_ops_t *ops = hal_sensor_get_ops();
    ops->bind(0U, &cfg);

    s_mock_di = false; /* 低电平 = 有效 */
    (void)ops->warmup(1U); (void)ops->warmup(1U);
    TEST_ASSERT_FALSE(ops->is_active(0U)); /* 未满 3 次 */
    (void)ops->warmup(1U);
    TEST_ASSERT_TRUE(ops->is_active(0U));
}

static void test_stable_count_ceiling_does_not_break_filter(void)
{
    /* stable_count 上限 255：超过后仍保持已激活状态，不溢出 */
    hal_sensor_bind_cfg_t cfg = {
        .pin = k_pin, .trig_count = 1U, .release_count = 1U,
    };
    const hal_sensor_ops_t *ops = hal_sensor_get_ops();
    ops->bind(0U, &cfg);

    s_mock_di = true;
    int i;
    for (i = 0; i < 300; i++)
    {
        (void)ops->warmup(1U);
    }
    TEST_ASSERT_TRUE(ops->is_active(0U)); /* 仍激活，无溢出崩溃 */
}

int main(void)
{
    UNITY_BEGIN();

    RUN_TEST(test_bind_null_cfg);
    RUN_TEST(test_bind_null_pin);
    RUN_TEST(test_bind_zero_trig_count);
    RUN_TEST(test_bind_zero_release_count);
    RUN_TEST(test_bind_valid);

    RUN_TEST(test_not_active_before_trig_count);
    RUN_TEST(test_active_after_trig_count);
    RUN_TEST(test_stays_active_before_release_count);
    RUN_TEST(test_released_after_release_count);
    RUN_TEST(test_interrupt_trig_resets_count);

    RUN_TEST(test_active_low_low_level_is_active);
    RUN_TEST(test_active_low_high_level_is_inactive);

    RUN_TEST(test_unbound_channel_returns_false);
    RUN_TEST(test_init_resets_runtime_state);
    RUN_TEST(test_no_io_ops_warmup_does_not_crash);

    RUN_TEST(test_trig_count_1_single_sample_activates);
    RUN_TEST(test_multiple_channels_are_independent);
    RUN_TEST(test_rebind_overwrites_config);
    RUN_TEST(test_active_low_with_debounce);
    RUN_TEST(test_stable_count_ceiling_does_not_break_filter);

    return UNITY_END();
}
