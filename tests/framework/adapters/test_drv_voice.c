/**
 * @file    test_drv_voice.c
 * @brief   drv_voice 单元测试
 *
 * drv_voice_t 无全局状态，每个测试可独立初始化新实例。
 * 分组：
 *   A. 参数校验
 *   B. 寄存器写入验证
 *   C. 通信事件
 *   D. 意外情况与边界
 */

#include "framework/adapters/outbound/hal/providers/snack/modbus/drv_voice.h"
#include "tests/support/modbus_stub.h"
#include "unity.h"

#include <string.h>

static volatile int s_last_event = 0;
static void event_cb(int code) { s_last_event = code; }

static drv_voice_t g_v;

/* 以默认参数初始化并注册回调（connect 默认成功） */
static void init_voice(void)
{
    TEST_ASSERT_EQUAL_INT(SW_OK, drv_voice_init(&g_v, "/dev/ttyS1", 9600, 2));
    drv_voice_register_event_cb(&g_v, event_cb);
    g_v.link.mb_connected = true;
}

void setUp(void)
{
    modbus_stub_reset();
    s_last_event = 0;
    memset(&g_v, 0, sizeof(g_v));
}

void tearDown(void) {}

/* =========================================================================
 * A. 参数校验
 * ========================================================================= */

static void test_init_null_instance(void)
{
    TEST_ASSERT_EQUAL_INT(SW_ERR_PARAM, drv_voice_init(NULL, "/dev/ttyS1", 9600, 2));
}

static void test_init_null_port(void)
{
    TEST_ASSERT_EQUAL_INT(SW_ERR_PARAM, drv_voice_init(&g_v, NULL, 9600, 2));
}

static void test_init_addr_zero(void)
{
    TEST_ASSERT_EQUAL_INT(SW_ERR_PARAM, drv_voice_init(&g_v, "/dev/ttyS1", 9600, 0));
}

static void test_init_addr_248(void)
{
    TEST_ASSERT_EQUAL_INT(SW_ERR_PARAM, drv_voice_init(&g_v, "/dev/ttyS1", 9600, 248));
}

static void test_play_not_init(void)
{
    TEST_ASSERT_EQUAL_INT(SW_ERR_NOT_INIT, drv_voice_play(&g_v, 1U));
}

static void test_stop_not_init(void)
{
    TEST_ASSERT_EQUAL_INT(SW_ERR_NOT_INIT, drv_voice_stop(&g_v));
}

static void test_pause_not_init(void)
{
    TEST_ASSERT_EQUAL_INT(SW_ERR_NOT_INIT, drv_voice_pause(&g_v));
}

static void test_set_volume_not_init(void)
{
    TEST_ASSERT_EQUAL_INT(SW_ERR_NOT_INIT, drv_voice_set_volume(&g_v, 10U));
}

static void test_volume_up_not_init(void)
{
    TEST_ASSERT_EQUAL_INT(SW_ERR_NOT_INIT, drv_voice_volume_up(&g_v));
}

static void test_volume_down_not_init(void)
{
    TEST_ASSERT_EQUAL_INT(SW_ERR_NOT_INIT, drv_voice_volume_down(&g_v));
}

/* =========================================================================
 * B. 寄存器写入验证
 *    寄存器地址来自驱动内部：
 *      PLAY=0x0004, STOP=0x000A, PAUSE=0x0009
 *      VOLUME=0x0002, VOL_UP=0x0005, VOL_DOWN=0x0006
 *    动作触发值 VOICE_CMD_TRIGGER = 0x0001
 * ========================================================================= */

static void test_play_writes_play_register(void)
{
    init_voice();
    TEST_ASSERT_EQUAL_INT(SW_OK, drv_voice_play(&g_v, 3U));
    TEST_ASSERT_EQUAL_INT(0x0004, modbus_stub_last_write_reg());
    TEST_ASSERT_EQUAL_UINT16(3U,   modbus_stub_last_write_val());
}

static void test_stop_writes_stop_register(void)
{
    init_voice();
    TEST_ASSERT_EQUAL_INT(SW_OK, drv_voice_stop(&g_v));
    TEST_ASSERT_EQUAL_INT(0x000A,   modbus_stub_last_write_reg());
    TEST_ASSERT_EQUAL_UINT16(0x0001U, modbus_stub_last_write_val());
}

static void test_pause_writes_pause_register(void)
{
    init_voice();
    TEST_ASSERT_EQUAL_INT(SW_OK, drv_voice_pause(&g_v));
    TEST_ASSERT_EQUAL_INT(0x0009, modbus_stub_last_write_reg());
}

static void test_set_volume_writes_volume_register(void)
{
    init_voice();
    TEST_ASSERT_EQUAL_INT(SW_OK, drv_voice_set_volume(&g_v, 15U));
    TEST_ASSERT_EQUAL_INT(0x0002,  modbus_stub_last_write_reg());
    TEST_ASSERT_EQUAL_UINT16(15U,  modbus_stub_last_write_val());
}

static void test_volume_up_writes_correct_register(void)
{
    init_voice();
    TEST_ASSERT_EQUAL_INT(SW_OK, drv_voice_volume_up(&g_v));
    TEST_ASSERT_EQUAL_INT(0x0005, modbus_stub_last_write_reg());
}

static void test_volume_down_writes_correct_register(void)
{
    init_voice();
    TEST_ASSERT_EQUAL_INT(SW_OK, drv_voice_volume_down(&g_v));
    TEST_ASSERT_EQUAL_INT(0x0006, modbus_stub_last_write_reg());
}

static void test_play_comm_fail_returns_err(void)
{
    init_voice();
    modbus_stub_set_write_result(-1);
    TEST_ASSERT_EQUAL_INT(SW_ERR_COMM, drv_voice_play(&g_v, 1U));
}

/* =========================================================================
 * C. 通信事件
 * ========================================================================= */

static void test_comm_lost_after_three_failures(void)
{
    init_voice();
    g_v.link.mb_connected = false;
    modbus_stub_set_connect_result(-1);
    drv_voice_play(&g_v, 1U);
    drv_voice_play(&g_v, 1U);
    drv_voice_play(&g_v, 1U);
    TEST_ASSERT_EQUAL_INT(DRV_VOICE_EVT_COMM_LOST, s_last_event);
    TEST_ASSERT_FALSE(g_v.comm_ok);
}

static void test_comm_restored_after_recovery(void)
{
    init_voice();
    g_v.comm_ok      = false;
    g_v.link.mb_connected = true;
    drv_voice_play(&g_v, 1U);
    TEST_ASSERT_EQUAL_INT(DRV_VOICE_EVT_COMM_RESTORED, s_last_event);
    TEST_ASSERT_TRUE(g_v.comm_ok);
}

static void test_init_success_when_connect_fails(void)
{
    modbus_stub_set_connect_result(-1);
    TEST_ASSERT_EQUAL_INT(SW_OK, drv_voice_init(&g_v, "/dev/ttyS1", 9600, 2));
    TEST_ASSERT_FALSE(g_v.link.mb_connected); /* defer-link，但 init 成功 */
}

/* =========================================================================
 * D. 意外情况与边界
 * ========================================================================= */

/* event_cb 设为 NULL 后触发通信失败，不应崩溃 */
static void test_null_event_cb_no_crash(void)
{
    init_voice();
    drv_voice_register_event_cb(&g_v, NULL);
    g_v.link.mb_connected = false;
    modbus_stub_set_connect_result(-1);
    drv_voice_play(&g_v, 1U);
    drv_voice_play(&g_v, 1U);
    drv_voice_play(&g_v, 1U); /* 此处本应触发 COMM_LOST，NULL 回调不崩溃即通过 */
}

/* VOICE_COMM_FAIL_RECONNECT=10：连续 10 次失败后，link 内部重连计数器应自动重置为 0 */
static void test_comm_fail_count_resets_at_reconnect_threshold(void)
{
    init_voice();
    g_v.link.mb_connected = false;
    modbus_stub_set_connect_result(-1); /* 每次重连都失败 */

    for (int i = 0; i < 10; i++) {
        drv_voice_play(&g_v, 1U);
    }

    /* fail_count 到达阈值 10 后被重置为 0（防止无限累积的安全阀） */
    TEST_ASSERT_EQUAL_UINT16(0U, g_v.link.comm_fail_count);
}

/* init 时 modbus_new_rtu 返回 NULL（上下文创建失败） */
static void test_init_modbus_ctx_fail(void)
{
    modbus_stub_set_new_rtu_fail(true);
    TEST_ASSERT_EQUAL_INT(SW_ERR_HW, drv_voice_init(&g_v, "/dev/ttyS1", 9600, 2));
    /* 失败后实例字段应保持零初始化（不可用）*/
    TEST_ASSERT_NULL(g_v.link.serial_port);
}

/* 同一个实例反复 init（上层配置变更场景），不应崩溃 */
static void test_reinit_same_instance(void)
{
    TEST_ASSERT_EQUAL_INT(SW_OK, drv_voice_init(&g_v, "/dev/ttyS1", 9600, 2));
    /* 再次初始化（新的 mutex init + modbus 上下文），不崩溃 */
    TEST_ASSERT_EQUAL_INT(SW_OK, drv_voice_init(&g_v, "/dev/ttyS1", 9600, 3));
    TEST_ASSERT_EQUAL_INT(3, g_v.link.modbus_addr);
}

/* =========================================================================
 * main
 * ========================================================================= */
int main(void)
{
    UNITY_BEGIN();

    /* A */
    RUN_TEST(test_init_null_instance);
    RUN_TEST(test_init_null_port);
    RUN_TEST(test_init_addr_zero);
    RUN_TEST(test_init_addr_248);
    RUN_TEST(test_play_not_init);
    RUN_TEST(test_stop_not_init);
    RUN_TEST(test_pause_not_init);
    RUN_TEST(test_set_volume_not_init);
    RUN_TEST(test_volume_up_not_init);
    RUN_TEST(test_volume_down_not_init);

    /* B */
    RUN_TEST(test_play_writes_play_register);
    RUN_TEST(test_stop_writes_stop_register);
    RUN_TEST(test_pause_writes_pause_register);
    RUN_TEST(test_set_volume_writes_volume_register);
    RUN_TEST(test_volume_up_writes_correct_register);
    RUN_TEST(test_volume_down_writes_correct_register);
    RUN_TEST(test_play_comm_fail_returns_err);

    /* C */
    RUN_TEST(test_comm_lost_after_three_failures);
    RUN_TEST(test_comm_restored_after_recovery);
    RUN_TEST(test_init_success_when_connect_fails);

    /* D */
    RUN_TEST(test_null_event_cb_no_crash);
    RUN_TEST(test_comm_fail_count_resets_at_reconnect_threshold);
    RUN_TEST(test_init_modbus_ctx_fail);
    RUN_TEST(test_reinit_same_instance);

    return UNITY_END();
}
