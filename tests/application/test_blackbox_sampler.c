/**
 * @file    test_blackbox_sampler.c
 * @brief   黑匣子默认通道与触发表单元测试
 */

#include "application/blackbox_sampler.h"
#include "common/event_types.h"
#include "common/sw_error.h"
#include "common/time_util.h"
#include "observability/recorder/blackbox_recorder.h"
#include "runtime/event_bus/event_bus.h"
#include "runtime/scheduler/periodic_task.h"
#include "wdf_test_spec.h"

sw_err_t periodic_task_register(const char        *name,
                                uint32_t           period_ms,
                                void (*fn)(void *),
                                void              *ctx,
                                int                policy,
                                int                priority,
                                size_t             stack_size)
{
    (void)name;
    (void)period_ms;
    (void)fn;
    (void)ctx;
    (void)policy;
    (void)priority;
    (void)stack_size;
    return SW_OK;
}

void setUp(void)
{
    time_util_init();
    blackbox_sampler_reset_for_test();
    TEST_ASSERT_EQUAL_INT(SW_OK, event_bus_init());
}

void tearDown(void)
{
    (void)event_bus_shutdown();
}

static void test_tick_records_snapshot_pack(void)
{
    blackbox_snapshot_info_t info;
    blackbox_sample_t        sample;

    TEST_ASSERT_EQUAL_INT(SW_OK, blackbox_sampler_start(1U, 0U, 0U));
    blackbox_sampler_tick();
    TEST_ASSERT_EQUAL_INT(SW_OK, blackbox_recorder_trigger(1U, 1U));
    TEST_ASSERT_EQUAL_INT(SW_OK, blackbox_recorder_get_info(&info));
    TEST_ASSERT_EQUAL_UINT32(1U, info.sample_count);
    TEST_ASSERT_EQUAL_INT(SW_OK, blackbox_recorder_copy_sample(0U, &sample));
    TEST_ASSERT_TRUE(sample.payload_size >= 7U);
}

static void test_start_rejects_empty_window(void)
{
    TEST_ASSERT_EQUAL_INT(SW_ERR_PARAM, blackbox_sampler_start(0U, 0U, 0U));
}

static void test_uninit_tick_is_noop(void)
{
    blackbox_sampler_tick();
    blackbox_sampler_on_watchdog_trip();
}

static void test_default_triggers_do_not_freeze(void)
{
    blackbox_snapshot_info_t info;

    TEST_ASSERT_EQUAL_INT(SW_OK, blackbox_sampler_start(1U, 0U, 0U));
    TEST_ASSERT_EQUAL_INT(SW_OK, event_publish(EVT_HW_ESTOP_ON, 0U));
    TEST_ASSERT_EQUAL_INT(SW_OK, event_bus_drain());
    blackbox_sampler_on_watchdog_trip();
    TEST_ASSERT_EQUAL_INT(SW_OK, blackbox_recorder_get_info(&info));
    TEST_ASSERT_EQUAL_INT(BLACKBOX_STATE_ARMED, info.state);
}

static void test_estop_trigger_freezes_when_enabled(void)
{
    blackbox_snapshot_info_t info;

    TEST_ASSERT_EQUAL_INT(SW_OK, blackbox_sampler_start(1U, 0U, BLACKBOX_TRIG_ESTOP));
    TEST_ASSERT_EQUAL_INT(SW_OK, event_publish(EVT_HW_ESTOP_ON, 0U));
    TEST_ASSERT_EQUAL_INT(SW_OK, event_bus_drain());
    TEST_ASSERT_EQUAL_INT(SW_OK, blackbox_recorder_get_info(&info));
    TEST_ASSERT_EQUAL_INT(BLACKBOX_STATE_FROZEN, info.state);
    TEST_ASSERT_EQUAL_UINT32((uint32_t)EVT_HW_ESTOP_ON, info.trigger_code);
}

static void test_watchdog_trigger_respects_mask(void)
{
    blackbox_snapshot_info_t info;

    TEST_ASSERT_EQUAL_INT(SW_OK, blackbox_sampler_start(1U, 0U, BLACKBOX_TRIG_WATCHDOG));
    blackbox_sampler_on_watchdog_trip();
    TEST_ASSERT_EQUAL_INT(SW_OK, blackbox_recorder_get_info(&info));
    TEST_ASSERT_EQUAL_INT(BLACKBOX_STATE_FROZEN, info.state);
    TEST_ASSERT_EQUAL_UINT32(BLACKBOX_TRIG_WATCHDOG, info.trigger_code);
}

static void test_start_is_idempotent(void)
{
    blackbox_snapshot_info_t info;

    TEST_ASSERT_EQUAL_INT(SW_OK, blackbox_sampler_start(1U, 0U, 0U));
    TEST_ASSERT_EQUAL_INT(SW_OK, blackbox_sampler_start(1U, 0U, BLACKBOX_TRIG_ESTOP));
    TEST_ASSERT_EQUAL_INT(SW_OK, event_publish(EVT_HW_ESTOP_ON, 0U));
    TEST_ASSERT_EQUAL_INT(SW_OK, event_bus_drain());
    TEST_ASSERT_EQUAL_INT(SW_OK, blackbox_recorder_get_info(&info));
    TEST_ASSERT_EQUAL_INT(BLACKBOX_STATE_ARMED, info.state);
}

int main(void)
{
    UNITY_BEGIN();
    WDF_RUN_TEST(test_start_rejects_empty_window, "", "验证 start 拒绝空窗口");
    WDF_RUN_TEST(test_uninit_tick_is_noop, "", "验证未 start 时采样与触发为空操作");
    WDF_RUN_TEST(test_tick_records_snapshot_pack, "", "验证采样写入快照精简包");
    WDF_RUN_TEST(test_default_triggers_do_not_freeze, "", "验证默认不自动冻窗口");
    WDF_RUN_TEST(test_estop_trigger_freezes_when_enabled, "", "验证勾选急停后冻窗口");
    WDF_RUN_TEST(test_watchdog_trigger_respects_mask, "", "验证看门狗仅在勾选后冻窗口");
    WDF_RUN_TEST(test_start_is_idempotent, "", "验证重复 start 保持首次配置");
    return UNITY_END();
}
