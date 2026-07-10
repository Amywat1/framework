/**
 * @file    test_pulse_out.c
 * @brief   pulse_out 脉冲时序原语单元测试
 */

#include "common/pulse_out.h"
#include "common/time_util.h"
#include "unity.h"

#include <stdint.h>

typedef struct {
    int  call_count;
    bool last_level;
} pulse_mock_ctx_t;

static sw_err_t mock_set_level(void *ctx, bool level)
{
    pulse_mock_ctx_t *mock = (pulse_mock_ctx_t *)ctx;
    mock->call_count++;
    mock->last_level = level;
    return SW_OK;
}

static pulse_out_slot_t make_slot(pulse_mock_ctx_t *ctx)
{
    pulse_out_slot_t slot;
    slot.ctx       = ctx;
    slot.set_level = mock_set_level;
    slot.active    = false;
    slot.start_ms  = 0U;
    slot.pulse_ms  = 0U;
    return slot;
}

void setUp(void)
{
}

void tearDown(void)
{
}

static void test_start_null_slot_returns_param_err(void)
{
    TEST_ASSERT_EQUAL_INT(SW_ERR_PARAM, pulse_out_start(NULL, 100U, 1000U));
}

static void test_start_null_callback_returns_param_err(void)
{
    pulse_out_slot_t slot;
    slot.ctx       = NULL;
    slot.set_level = NULL;
    slot.active    = false;
    slot.start_ms  = 0U;
    slot.pulse_ms  = 0U;
    TEST_ASSERT_EQUAL_INT(SW_ERR_PARAM, pulse_out_start(&slot, 100U, 1000U));
}

static void test_start_zero_pulse_ms_returns_param_err(void)
{
    pulse_mock_ctx_t ctx  = {0, false};
    pulse_out_slot_t slot = make_slot(&ctx);
    TEST_ASSERT_EQUAL_INT(SW_ERR_PARAM, pulse_out_start(&slot, 0U, 1000U));
}

static void test_start_sets_active_and_pulls_high(void)
{
    pulse_mock_ctx_t ctx  = {0, false};
    pulse_out_slot_t slot = make_slot(&ctx);

    TEST_ASSERT_EQUAL_INT(SW_OK, pulse_out_start(&slot, 50U, 1000U));
    TEST_ASSERT_TRUE(pulse_out_is_active(&slot));
    TEST_ASSERT_EQUAL_INT(1, ctx.call_count);
    TEST_ASSERT_TRUE(ctx.last_level);
    TEST_ASSERT_EQUAL_UINT32(50U, slot.pulse_ms);
}

static void test_tick_before_expiry_stays_active(void)
{
    pulse_mock_ctx_t ctx  = {0, false};
    pulse_out_slot_t slot = make_slot(&ctx);

    pulse_out_start(&slot, 100U, 1000U);
    pulse_out_tick(&slot, 1099U);

    TEST_ASSERT_TRUE(pulse_out_is_active(&slot));
    TEST_ASSERT_EQUAL_INT(1, ctx.call_count);
    TEST_ASSERT_TRUE(ctx.last_level);
}

static void test_tick_at_expiry_pulls_low(void)
{
    pulse_mock_ctx_t ctx  = {0, false};
    pulse_out_slot_t slot = make_slot(&ctx);

    pulse_out_start(&slot, 100U, 1000U);
    pulse_out_tick(&slot, 1100U);

    TEST_ASSERT_FALSE(pulse_out_is_active(&slot));
    TEST_ASSERT_EQUAL_INT(2, ctx.call_count);
    TEST_ASSERT_FALSE(ctx.last_level);
}

static void test_cancel_during_active_pulls_low(void)
{
    pulse_mock_ctx_t ctx  = {0, false};
    pulse_out_slot_t slot = make_slot(&ctx);

    pulse_out_start(&slot, 500U, 2000U);
    pulse_out_cancel(&slot);

    TEST_ASSERT_FALSE(pulse_out_is_active(&slot));
    TEST_ASSERT_EQUAL_INT(2, ctx.call_count);
    TEST_ASSERT_FALSE(ctx.last_level);
}

static void test_cancel_when_inactive_is_noop(void)
{
    pulse_mock_ctx_t ctx  = {0, false};
    pulse_out_slot_t slot = make_slot(&ctx);

    pulse_out_cancel(&slot);
    TEST_ASSERT_FALSE(pulse_out_is_active(&slot));
    TEST_ASSERT_EQUAL_INT(0, ctx.call_count);
}

static void test_is_active_null_returns_false(void)
{
    TEST_ASSERT_FALSE(pulse_out_is_active(NULL));
}

static void test_tick_null_slot_is_safe(void)
{
    pulse_out_tick(NULL, 1000U);
}

static void test_elapsed_ms_basic(void)
{
    TEST_ASSERT_EQUAL_UINT32(100U, time_elapsed_ms(1000ULL, 1100ULL));
}

static void test_elapsed_ms_wraparound(void)
{
    TEST_ASSERT_EQUAL_UINT32(100U, time_elapsed_ms(UINT64_MAX - 99ULL, 0ULL));
}

int main(void)
{
    time_util_init();
    UNITY_BEGIN();

    RUN_TEST(test_start_null_slot_returns_param_err);
    RUN_TEST(test_start_null_callback_returns_param_err);
    RUN_TEST(test_start_zero_pulse_ms_returns_param_err);
    RUN_TEST(test_start_sets_active_and_pulls_high);
    RUN_TEST(test_tick_before_expiry_stays_active);
    RUN_TEST(test_tick_at_expiry_pulls_low);
    RUN_TEST(test_cancel_during_active_pulls_low);
    RUN_TEST(test_cancel_when_inactive_is_noop);
    RUN_TEST(test_is_active_null_returns_false);
    RUN_TEST(test_tick_null_slot_is_safe);
    RUN_TEST(test_elapsed_ms_basic);
    RUN_TEST(test_elapsed_ms_wraparound);

    return UNITY_END();
}
