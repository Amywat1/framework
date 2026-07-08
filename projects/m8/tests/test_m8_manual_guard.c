/**
 * @file    test_m8_manual_guard.c
 * @brief   M8 手动点动准入守卫单元测试
 * @author  HUWANGWEI
 * @date    2026-07-08
 */

#include "projects/m8/adapters/manual/m8_manual_guard.h"
#include "framework/services/dev_ctx/dev_ctx.h"
#include "framework/adapters/outbound/hal/components/sensor_filter/hal_sensor_filter.h"
#include "framework/ports/outbound/hal/hal_io_port.h"
#include "framework/ports/outbound/hal/hal_sensor_port.h"
#include "projects/m8/bindings/m8_sensor.h"
#include "projects/m8/config/m8_signal_table.h"
#include "framework/common/io_handle.h"
#include "unity.h"

#include <stdbool.h>

static bool s_mock_di = false;

static bool mock_di_read(io_di_t pin)
{
    (void)pin;
    return s_mock_di;
}

static const hal_io_ops_t s_mock_io_ops = {
    .di_read = mock_di_read,
};

void setUp(void)
{
    /* 急停为 active_low：mock DI 高电平表示未触发 */
    s_mock_di = true;
    (void)dev_ctx_init();
    hal_io_register(&s_mock_io_ops);
    hal_sensor_filter_register();
    (void)m8_sensor_setup();
    (void)m8_sensor_warmup();
}

void tearDown(void) {}

static void test_allow_idle_stop_fault(void)
{
    dev_state_t states[] = { DEV_STATE_IDLE, DEV_STATE_STOP, DEV_STATE_FAULT };
    size_t      i;

    for (i = 0U; i < sizeof(states) / sizeof(states[0]); ++i)
    {
        dev_ctx_set_device_state(states[i]);
        TEST_ASSERT_EQUAL_INT(SW_OK, m8_manual_guard_allow_motion());
    }
}

static void test_reject_running_init(void)
{
    dev_ctx_set_device_state(DEV_STATE_RUNNING);
    TEST_ASSERT_EQUAL_INT(SW_ERR_STATE, m8_manual_guard_allow_motion());

    dev_ctx_set_device_state(DEV_STATE_INIT);
    TEST_ASSERT_EQUAL_INT(SW_ERR_STATE, m8_manual_guard_allow_motion());
}

static void test_reject_estop(void)
{
    dev_ctx_set_device_state(DEV_STATE_IDLE);
    s_mock_di = false;
    (void)hal_sensor_get_ops()->warmup(1U);
    TEST_ASSERT_TRUE(m8_signal_is_active(M8_SIG_ESTOP));
    TEST_ASSERT_EQUAL_INT(SW_ERR_STATE, m8_manual_guard_allow_motion());
    s_mock_di = true;
    (void)hal_sensor_get_ops()->warmup(1U);
}

static void test_stop_always_allowed(void)
{
    dev_ctx_set_device_state(DEV_STATE_RUNNING);
    TEST_ASSERT_EQUAL_INT(SW_OK, m8_manual_guard_allow_stop());
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_allow_idle_stop_fault);
    RUN_TEST(test_reject_running_init);
    RUN_TEST(test_reject_estop);
    RUN_TEST(test_stop_always_allowed);
    return UNITY_END();
}
