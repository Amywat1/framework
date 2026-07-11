/**
 * @file    test_hal_voice_sim.c
 * @brief   hal_voice_sim 语音 HAL 仿真单元测试
 *
 * 分组：
 *   A. 注册
 *   B. ops 调用
 */

#include "common/sw_error.h"
#include "ports/outbound/hal/hal_voice_port.h"
#include "unity.h"

#include <stdint.h>

void hal_voice_sim_register(void);

static const hal_voice_ops_t *voice(void)
{
    return hal_voice_get_ops();
}

void setUp(void)
{
    hal_voice_sim_register();
}

void tearDown(void)
{
}

static void test_register_returns_ops(void)
{
    TEST_ASSERT_NOT_NULL(voice());
    TEST_ASSERT_NOT_NULL(voice()->init);
    TEST_ASSERT_NOT_NULL(voice()->play);
    TEST_ASSERT_NOT_NULL(voice()->stop);
    TEST_ASSERT_NOT_NULL(voice()->pause);
    TEST_ASSERT_NOT_NULL(voice()->set_volume);
    TEST_ASSERT_NOT_NULL(voice()->volume_up);
    TEST_ASSERT_NOT_NULL(voice()->volume_down);
    TEST_ASSERT_NOT_NULL(voice()->register_event_cb);
}

static void test_init_returns_ok(void)
{
    TEST_ASSERT_EQUAL_INT(SW_OK, voice()->init());
}

static void test_play_returns_ok(void)
{
    TEST_ASSERT_EQUAL_INT(SW_OK, voice()->play(42U));
}

static void test_stop_pause_volume_ops_return_ok(void)
{
    TEST_ASSERT_EQUAL_INT(SW_OK, voice()->stop());
    TEST_ASSERT_EQUAL_INT(SW_OK, voice()->pause());
    TEST_ASSERT_EQUAL_INT(SW_OK, voice()->set_volume(80U));
    TEST_ASSERT_EQUAL_INT(SW_OK, voice()->volume_up());
    TEST_ASSERT_EQUAL_INT(SW_OK, voice()->volume_down());
}

static void test_register_event_cb_accepts_null(void)
{
    voice()->register_event_cb(NULL);
}

int main(void)
{
    UNITY_BEGIN();

    RUN_TEST(test_register_returns_ops);
    RUN_TEST(test_init_returns_ok);
    RUN_TEST(test_play_returns_ok);
    RUN_TEST(test_stop_pause_volume_ops_return_ok);
    RUN_TEST(test_register_event_cb_accepts_null);

    return UNITY_END();
}
