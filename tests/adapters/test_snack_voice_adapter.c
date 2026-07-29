/**
 * @file    test_snack_voice_adapter.c
 * @brief   Snack Modbus voice HAL provider 单元测试。
 */

#include "adapters/outbound/hal/providers/snack/modbus/drv_voice.h"
#include "adapters/outbound/hal/providers/snack/modbus/snack_voice_adapter.h"
#include "ports/outbound/hal/hal_voice_port.h"
#include "tests/stubs/snack/drv_modbus_link_fake.h"
#include "unity.h"

#include <string.h>

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
    snack_voice_adapter_register(NULL, 0, 0);
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
    snack_voice_adapter_register("/dev/ttyS1", 9600, 7);
    TEST_ASSERT_EQUAL_INT(SW_OK, voice()->init());
    TEST_ASSERT_TRUE(snack_modbus_fake_init_called());
    TEST_ASSERT_EQUAL_STRING("/dev/ttyS1", snack_modbus_fake_serial_port());
    TEST_ASSERT_EQUAL_INT(9600, snack_modbus_fake_baud());
    TEST_ASSERT_EQUAL_INT(7, snack_modbus_fake_addr());
}

static void test_commands_write_expected_registers(void)
{
    snack_voice_adapter_register("/dev/ttyS1", 9600, 7);
    TEST_ASSERT_EQUAL_INT(SW_OK, voice()->init());

    TEST_ASSERT_EQUAL_INT(SW_OK, voice()->play(12));
    TEST_ASSERT_EQUAL_UINT16(0x0004U, snack_modbus_fake_last_write_addr());
    TEST_ASSERT_EQUAL_UINT16(12U, snack_modbus_fake_last_write_val());

    TEST_ASSERT_EQUAL_INT(SW_OK, voice()->set_volume(20));
    TEST_ASSERT_EQUAL_UINT16(0x0002U, snack_modbus_fake_last_write_addr());
    TEST_ASSERT_EQUAL_UINT16(20U, snack_modbus_fake_last_write_val());

    TEST_ASSERT_EQUAL_INT(SW_OK, voice()->volume_up());
    TEST_ASSERT_EQUAL_UINT16(0x0005U, snack_modbus_fake_last_write_addr());
    TEST_ASSERT_EQUAL_UINT16(1U, snack_modbus_fake_last_write_val());

    TEST_ASSERT_EQUAL_INT(SW_OK, voice()->volume_down());
    TEST_ASSERT_EQUAL_UINT16(0x0006U, snack_modbus_fake_last_write_addr());
    TEST_ASSERT_EQUAL_UINT16(1U, snack_modbus_fake_last_write_val());

    TEST_ASSERT_EQUAL_INT(SW_OK, voice()->pause());
    TEST_ASSERT_EQUAL_UINT16(0x0009U, snack_modbus_fake_last_write_addr());
    TEST_ASSERT_EQUAL_UINT16(1U, snack_modbus_fake_last_write_val());

    TEST_ASSERT_EQUAL_INT(SW_OK, voice()->stop());
    TEST_ASSERT_EQUAL_UINT16(0x000AU, snack_modbus_fake_last_write_addr());
    TEST_ASSERT_EQUAL_UINT16(1U, snack_modbus_fake_last_write_val());
}

static void test_comm_lost_and_restored_events_are_reported(void)
{
    snack_voice_adapter_register("/dev/ttyS1", 9600, 7);
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
    TEST_ASSERT_EQUAL_INT(DRV_VOICE_EVT_COMM_LOST, s_events[0]);

    TEST_ASSERT_EQUAL_INT(SW_OK, voice()->play(1));
    TEST_ASSERT_EQUAL_UINT(2U, s_event_count);
    TEST_ASSERT_EQUAL_INT(DRV_VOICE_EVT_COMM_RESTORED, s_events[1]);
}

static void test_instance_init_failure_keeps_adapter_not_ready(void)
{
    snack_modbus_fake_set_init_result(SW_ERR_HW);

    snack_voice_adapter_register("/dev/ttyS1", 9600, 7);
    TEST_ASSERT_EQUAL_INT(SW_ERR_HW, voice()->init());
    TEST_ASSERT_EQUAL_INT(SW_ERR_NOT_INIT, voice()->play(1));
}

int main(void)
{
    UNITY_BEGIN();

    RUN_TEST(test_registered_ops_reject_commands_before_instance_init);
    RUN_TEST(test_instance_init_passes_modbus_parameters);
    RUN_TEST(test_commands_write_expected_registers);
    RUN_TEST(test_comm_lost_and_restored_events_are_reported);
    RUN_TEST(test_instance_init_failure_keeps_adapter_not_ready);

    return UNITY_END();
}
