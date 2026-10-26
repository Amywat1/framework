/**
 * @file    test_snack_voice_adapter.c
 * @brief   Snack Modbus voice HAL provider 单元测试。
 */

#include "adapters/outbound/hal/providers/snack/modbus/snack_voice_adapter.h"
#include "domain/ports/outbound/hal/hal_voice_port.h"
#include "tests/stubs/snack/drv_modbus_link_fake.h"
#include "wdf_test_spec.h"

#include <string.h>

static const drv_voice_modbus_profile_t s_profile = {
    .reg_volume   = 0x0011U,
    .reg_play     = 0x0012U,
    .reg_vol_up   = 0x0013U,
    .reg_vol_down = 0x0014U,
    .reg_pause    = 0x0015U,
    .reg_stop     = 0x0016U,
    .trigger      = 0x0003U,
};

static int      s_events[8];
static unsigned s_event_count;

static void event_cb(int event_code)
{
    if (s_event_count < (sizeof(s_events) / sizeof(s_events[0]))) {
        s_events[s_event_count++] = event_code;
    }
}

static const hal_voice_ops_t *voice(void)
{
    const hal_voice_ops_t *ops = hal_voice_get_ops();

    TEST_ASSERT_NOT_NULL(ops);
    return ops;
}

void setUp(void)
{
    snack_modbus_fake_reset();
    memset(s_events, 0, sizeof(s_events));
    s_event_count = 0;
    snack_voice_adapter_test_reset();
    snack_voice_adapter_register(NULL, 0, 0, NULL);
}

void tearDown(void)
{
}

static void test_registered_ops_reject_commands_before_instance_init(void)
{
    TEST_ASSERT_EQUAL_INT(SW_ERR_NOT_INIT, voice()->init());
    TEST_ASSERT_EQUAL_INT(SW_ERR_NOT_INIT, voice()->play(3));
    TEST_ASSERT_EQUAL_INT(SW_ERR_NOT_INIT, voice()->stop());
    TEST_ASSERT_EQUAL_INT(SW_ERR_NOT_INIT, voice()->pause());
    TEST_ASSERT_EQUAL_INT(SW_ERR_NOT_INIT, voice()->set_volume(8));
    TEST_ASSERT_EQUAL_INT(SW_ERR_NOT_INIT, voice()->volume_up());
    TEST_ASSERT_EQUAL_INT(SW_ERR_NOT_INIT, voice()->volume_down());
}

static void test_instance_init_passes_modbus_parameters(void)
{
    snack_voice_adapter_register("/dev/ttyS1", 9600, 7, &s_profile);
    TEST_ASSERT_EQUAL_INT(SW_OK, voice()->init());
    TEST_ASSERT_TRUE(snack_modbus_fake_init_called());
    TEST_ASSERT_EQUAL_STRING("/dev/ttyS1", snack_modbus_fake_serial_port());
    TEST_ASSERT_EQUAL_INT(9600, snack_modbus_fake_baud());
    TEST_ASSERT_EQUAL_INT(7, snack_modbus_fake_addr());
}

static void test_commands_write_expected_registers(void)
{
    snack_voice_adapter_register("/dev/ttyS1", 9600, 7, &s_profile);
    TEST_ASSERT_EQUAL_INT(SW_OK, voice()->init());

    TEST_ASSERT_EQUAL_INT(SW_OK, voice()->play(12));
    TEST_ASSERT_EQUAL_UINT16(s_profile.reg_play, snack_modbus_fake_last_write_addr());
    TEST_ASSERT_EQUAL_UINT16(12U, snack_modbus_fake_last_write_val());

    TEST_ASSERT_EQUAL_INT(SW_OK, voice()->set_volume(20));
    TEST_ASSERT_EQUAL_UINT16(s_profile.reg_volume, snack_modbus_fake_last_write_addr());
    TEST_ASSERT_EQUAL_UINT16(20U, snack_modbus_fake_last_write_val());

    TEST_ASSERT_EQUAL_INT(SW_OK, voice()->volume_up());
    TEST_ASSERT_EQUAL_UINT16(s_profile.reg_vol_up, snack_modbus_fake_last_write_addr());
    TEST_ASSERT_EQUAL_UINT16(s_profile.trigger, snack_modbus_fake_last_write_val());

    TEST_ASSERT_EQUAL_INT(SW_OK, voice()->volume_down());
    TEST_ASSERT_EQUAL_UINT16(s_profile.reg_vol_down, snack_modbus_fake_last_write_addr());
    TEST_ASSERT_EQUAL_UINT16(s_profile.trigger, snack_modbus_fake_last_write_val());

    TEST_ASSERT_EQUAL_INT(SW_OK, voice()->pause());
    TEST_ASSERT_EQUAL_UINT16(s_profile.reg_pause, snack_modbus_fake_last_write_addr());
    TEST_ASSERT_EQUAL_UINT16(s_profile.trigger, snack_modbus_fake_last_write_val());

    TEST_ASSERT_EQUAL_INT(SW_OK, voice()->stop());
    TEST_ASSERT_EQUAL_UINT16(s_profile.reg_stop, snack_modbus_fake_last_write_addr());
    TEST_ASSERT_EQUAL_UINT16(s_profile.trigger, snack_modbus_fake_last_write_val());
}

static void test_comm_lost_and_restored_events_are_reported(void)
{
    snack_voice_adapter_register("/dev/ttyS1", 9600, 7, &s_profile);
    voice()->register_event_cb(event_cb);
    TEST_ASSERT_EQUAL_INT(SW_OK, voice()->init());

    snack_modbus_fake_push_write_result(SW_ERR_COMM);
    snack_modbus_fake_push_write_result(SW_ERR_COMM);
    snack_modbus_fake_push_write_result(SW_ERR_COMM);
    snack_modbus_fake_push_write_result(SW_OK);

    TEST_ASSERT_EQUAL_INT(SW_ERR_COMM, voice()->play(1));
    TEST_ASSERT_EQUAL_INT(SW_ERR_COMM, voice()->play(1));
    TEST_ASSERT_EQUAL_INT(SW_ERR_COMM, voice()->play(1));
    TEST_ASSERT_EQUAL_UINT(1U, s_event_count);
    TEST_ASSERT_EQUAL_INT(HAL_VOICE_EVT_COMM_LOST, s_events[0]);

    TEST_ASSERT_EQUAL_INT(SW_OK, voice()->play(1));
    TEST_ASSERT_EQUAL_UINT(2U, s_event_count);
    TEST_ASSERT_EQUAL_INT(HAL_VOICE_EVT_COMM_RESTORED, s_events[1]);
}

static void test_instance_init_failure_keeps_adapter_not_ready(void)
{
    snack_modbus_fake_set_init_result(SW_ERR_HW);

    snack_voice_adapter_register("/dev/ttyS1", 9600, 7, &s_profile);
    TEST_ASSERT_EQUAL_INT(SW_ERR_HW, voice()->init());
    TEST_ASSERT_EQUAL_INT(SW_ERR_NOT_INIT, voice()->play(1));
}

static void test_instance_init_rejects_missing_profile(void)
{
    snack_voice_adapter_register("/dev/ttyS1", 9600, 7, NULL);
    TEST_ASSERT_EQUAL_INT(SW_ERR_PARAM, voice()->init());
    TEST_ASSERT_EQUAL_INT(SW_ERR_NOT_INIT, voice()->play(1));
}

int main(void)
{
    UNITY_BEGIN();

    WDF_RUN_TEST(test_registered_ops_reject_commands_before_instance_init, "", "验证实例初始化前已注册接口拒绝命令");
    WDF_RUN_TEST(test_instance_init_passes_modbus_parameters, "", "验证实例初始化向 Modbus 传递配置参数");
    WDF_RUN_TEST(test_commands_write_expected_registers, "", "验证语音命令写入预期寄存器");
    WDF_RUN_TEST(test_comm_lost_and_restored_events_are_reported, "", "验证通信丢失和恢复事件被上报");
    WDF_RUN_TEST(test_instance_init_failure_keeps_adapter_not_ready, "", "验证实例初始化失败保持适配器未就绪");
    WDF_RUN_TEST(test_instance_init_rejects_missing_profile, "", "验证缺少寄存器 profile 时实例初始化失败");

    return UNITY_END();
}
