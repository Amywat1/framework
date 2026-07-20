/**
 * @file    test_snack_io_exp_adapter.c
 * @brief   Snack io_exp IO HAL provider 单元测试。
 */

#include "adapters/outbound/hal/providers/snack/io_exp/io_exp_driver.h"
#include "adapters/outbound/hal/providers/snack/io_exp/snack_io_adapter.h"
#include "common/io_handle.h"
#include "ports/outbound/hal/hal_io_port.h"
#include "tests/stubs/io_exp/io_exp_fake.h"
#include "unity.h"

#include <string.h>

static const drv_io_name_entry_t s_di_names[] = {
    {"DI_START", IO_HANDLE_MAKE(IO_KIND_DI, 1U, 1U)},
    {"DI_STOP",  IO_HANDLE_MAKE(IO_KIND_DI, 2U, 1U)},
};
static const drv_io_name_entry_t s_do_names[] = {
    {"DO_RELAY", IO_HANDLE_MAKE(IO_KIND_DO, 1U, 2U)},
    {"DO_LAMP",  IO_HANDLE_MAKE(IO_KIND_DO, 2U, 3U)},
};

static drv_io_cfg_t make_cfg(void)
{
    drv_io_cfg_t cfg = {
        .can_bus     = "can0",
        .can_baud    = 500000,
        .self_node   = 9,
        .board_count = 2,
        .pin_count   = 8,
        .di_table    = s_di_names,
        .di_count    = sizeof(s_di_names) / sizeof(s_di_names[0]),
        .do_table    = s_do_names,
        .do_count    = sizeof(s_do_names) / sizeof(s_do_names[0]),
    };

    return cfg;
}

static const hal_io_ops_t *io_ops(void)
{
    const hal_io_ops_t *ops = hal_io_get_ops();

    TEST_ASSERT_NOT_NULL(ops);
    return ops;
}

void setUp(void)
{
    drv_io_cfg_t cfg = make_cfg();

    io_exp_fake_reset();
    snack_io_adapter_test_reset();
    snack_io_adapter_register();
    TEST_ASSERT_EQUAL_INT(SW_OK, snack_io_adapter_configure(&cfg));
    TEST_ASSERT_EQUAL_INT(SW_OK, io_ops()->init());
}

void tearDown(void)
{
}

static void test_sdk_init_registers_internal_log_and_delegates_to_io_exp_sdk(void)
{
    io_exp_fake_set_init_result(0);
    TEST_ASSERT_EQUAL_INT(SW_OK, io_exp_driver_sdk_init("can0", 500000, 9, 2));
    TEST_ASSERT_EQUAL_STRING("can0", io_exp_fake_can_bus());
    TEST_ASSERT_EQUAL_INT(500000, io_exp_fake_can_baud());
    TEST_ASSERT_EQUAL_INT(9, io_exp_fake_self_node());
    TEST_ASSERT_EQUAL_INT(2, io_exp_fake_board_count());

    io_exp_fake_set_init_result(-1);
    TEST_ASSERT_EQUAL_INT(SW_ERR_HW, io_exp_driver_sdk_init("can1", 250000, 1, 1));
    TEST_ASSERT_TRUE(io_exp_fake_log_api_set());
}

static void test_init_rejects_invalid_config(void)
{
    drv_io_cfg_t cfg = make_cfg();

    TEST_ASSERT_EQUAL_INT(SW_ERR_PARAM, drv_io_cfg_validate(NULL));
    TEST_ASSERT_EQUAL_INT(SW_ERR_PARAM, drv_io_init(NULL));

    cfg             = make_cfg();
    cfg.can_bus     = NULL;
    TEST_ASSERT_EQUAL_INT(SW_ERR_PARAM, drv_io_cfg_validate(&cfg));

    cfg             = make_cfg();
    cfg.board_count = 0;
    TEST_ASSERT_EQUAL_INT(SW_ERR_PARAM, drv_io_cfg_validate(&cfg));
    TEST_ASSERT_EQUAL_INT(SW_ERR_PARAM, drv_io_init(&cfg));

    cfg           = make_cfg();
    cfg.pin_count = 0;
    TEST_ASSERT_EQUAL_INT(SW_ERR_PARAM, drv_io_cfg_validate(&cfg));
    TEST_ASSERT_EQUAL_INT(SW_ERR_PARAM, drv_io_init(&cfg));

    cfg           = make_cfg();
    cfg.pin_count = 33;
    TEST_ASSERT_EQUAL_INT(SW_ERR_PARAM, drv_io_cfg_validate(&cfg));
    TEST_ASSERT_EQUAL_INT(SW_ERR_PARAM, drv_io_init(&cfg));
}

static void test_hal_adapter_validates_cfg_before_sdk_init(void)
{
    drv_io_cfg_t cfg = make_cfg();

    io_exp_fake_reset();
    snack_io_adapter_test_reset();
    snack_io_adapter_register();
    cfg.pin_count = 33;
    TEST_ASSERT_EQUAL_INT(SW_ERR_PARAM, snack_io_adapter_configure(&cfg));
    TEST_ASSERT_EQUAL_INT(SW_ERR_NOT_INIT, io_ops()->init());
    TEST_ASSERT_NULL(io_exp_fake_can_bus());
}

static void test_hal_adapter_rejects_init_before_configure_and_duplicate_configure(void)
{
    drv_io_cfg_t cfg = make_cfg();

    snack_io_adapter_test_reset();
    snack_io_adapter_register();
    TEST_ASSERT_EQUAL_INT(SW_ERR_NOT_INIT, io_ops()->init());
    TEST_ASSERT_EQUAL_INT(SW_ERR_NOT_INIT, io_ops()->start());
    TEST_ASSERT_EQUAL_INT(SW_OK, snack_io_adapter_configure(&cfg));
    TEST_ASSERT_EQUAL_INT(SW_ERR_BUSY, snack_io_adapter_configure(&cfg));
}

static void test_hal_adapter_exposes_name_resolution_and_board_count(void)
{
    io_di_t di;
    io_do_t dout;

    TEST_ASSERT_EQUAL_INT(2, io_ops()->board_count());
    TEST_ASSERT_TRUE(io_ops()->try_parse_di("DI_START", &di));
    TEST_ASSERT_EQUAL_UINT16(IO_HANDLE_MAKE(IO_KIND_DI, 1U, 1U), di.raw);
    TEST_ASSERT_TRUE(io_ops()->try_parse_di("START", &di));
    TEST_ASSERT_EQUAL_UINT16(IO_HANDLE_MAKE(IO_KIND_DI, 1U, 1U), di.raw);
    TEST_ASSERT_FALSE(io_ops()->try_parse_di("MISSING", &di));

    TEST_ASSERT_TRUE(io_ops()->try_parse_do("DO_RELAY", &dout));
    TEST_ASSERT_EQUAL_UINT16(IO_HANDLE_MAKE(IO_KIND_DO, 1U, 2U), dout.raw);
    TEST_ASSERT_TRUE(io_ops()->try_parse_do("RELAY", &dout));
    TEST_ASSERT_EQUAL_STRING("DI_START", io_ops()->di_name(IO_DI(1U, 1U)));
    TEST_ASSERT_EQUAL_STRING("DO_RELAY", io_ops()->do_name(IO_DO(1U, 2U)));
}

static void test_do_set_updates_stats_and_rejects_invalid_pin(void)
{
    hal_io_stats_t stats;

    TEST_ASSERT_EQUAL_INT(SW_OK, io_ops()->do_set(IO_DO(1U, 2U), true));
    TEST_ASSERT_EQUAL_INT(SW_OK, io_ops()->get_stats(1, &stats));
    TEST_ASSERT_TRUE(stats.dirty_pending);
    TEST_ASSERT_EQUAL_UINT32(1U, stats.output_request_count);
    TEST_ASSERT_EQUAL_UINT32(0x00000002U, stats.last_output_snapshot);

    TEST_ASSERT_EQUAL_INT(SW_ERR_PARAM, io_ops()->do_set(IO_DO(9U, 1U), true));
    TEST_ASSERT_EQUAL_INT(SW_ERR_PARAM, io_ops()->get_stats(0, &stats));
    TEST_ASSERT_EQUAL_INT(SW_ERR_PARAM, io_ops()->get_stats(1, NULL));
}

static void test_di_test_override_controls_read_value(void)
{
    io_di_t pin = IO_DI(1U, 1U);

    TEST_ASSERT_FALSE(io_ops()->di_read(pin));
    drv_io_set_test_override(pin, 1);
    TEST_ASSERT_TRUE(io_ops()->di_read(pin));
    drv_io_set_test_override(pin, 0);
    TEST_ASSERT_FALSE(io_ops()->di_read(pin));
    drv_io_clear_test_override(pin);
    TEST_ASSERT_FALSE(io_ops()->di_read(pin));
}

static void test_wait_boards_online_uses_sdk_probe(void)
{
    io_exp_fake_set_online(1, 1);
    io_exp_fake_set_online(2, 1);
    TEST_ASSERT_EQUAL_INT(SW_OK, io_ops()->wait_boards_online(10U));

    io_exp_fake_set_online(2, 0);
    TEST_ASSERT_EQUAL_INT(SW_ERR_TIMEOUT, io_ops()->wait_boards_online(1U));
}

static void test_pulse_read_and_clear_delegate_to_sdk(void)
{
    io_exp_fake_set_pulse(1, 1, 77);
    TEST_ASSERT_EQUAL_INT(77, io_ops()->pulse_read(IO_DI(1U, 1U)));
    TEST_ASSERT_EQUAL_INT(-1, io_ops()->pulse_read(IO_DI(9U, 1U)));

    TEST_ASSERT_EQUAL_INT(SW_OK, io_ops()->pulse_clear(IO_DI(1U, 1U)));
    TEST_ASSERT_TRUE(io_exp_fake_sdo_called());
    TEST_ASSERT_EQUAL_INT(1, io_exp_fake_sdo_board());
    TEST_ASSERT_EQUAL_INT(0x2005, io_exp_fake_sdo_index());
    TEST_ASSERT_EQUAL_INT(1, io_exp_fake_sdo_sub_index());
    TEST_ASSERT_EQUAL_INT(0, io_exp_fake_sdo_data());

    io_exp_fake_set_sdo_result(-1);
    TEST_ASSERT_EQUAL_INT(SW_ERR_COMM, io_ops()->pulse_clear(IO_DI(1U, 1U)));
    TEST_ASSERT_EQUAL_INT(SW_ERR_PARAM, io_ops()->pulse_clear(IO_DI(9U, 1U)));
}

static void test_adc_read_delegates_to_sdk(void)
{
    TEST_ASSERT_EQUAL_INT(DRV_IO_ADC_ERR_NOT_INIT, io_ops()->adc_read(1, 1));
    TEST_ASSERT_EQUAL_INT(DRV_IO_ADC_ERR_NOT_INIT, io_ops()->adc_mv(1, 2));
    TEST_ASSERT_EQUAL_INT(DRV_IO_ADC_ERR_NOT_INIT, io_ops()->adc_ma(1, 3));

    io_exp_fake_set_adc(1, 1, 100, 2500, 12);
    TEST_ASSERT_EQUAL_INT(100, io_ops()->adc_read(1, 1));
    TEST_ASSERT_EQUAL_INT(2500, io_ops()->adc_mv(1, 1));
    TEST_ASSERT_EQUAL_INT(12, io_ops()->adc_ma(1, 1));

    TEST_ASSERT_EQUAL_INT(-1, io_ops()->adc_read(0, 1));
    TEST_ASSERT_EQUAL_INT(-1, io_ops()->adc_mv(1, 0));
    TEST_ASSERT_EQUAL_INT(-1, io_ops()->adc_ma(1, 5));
    TEST_ASSERT_EQUAL_INT(-1, io_ops()->adc_read(9, 1));
}

int main(void)
{
    UNITY_BEGIN();

    RUN_TEST(test_sdk_init_registers_internal_log_and_delegates_to_io_exp_sdk);
    RUN_TEST(test_init_rejects_invalid_config);
    RUN_TEST(test_hal_adapter_validates_cfg_before_sdk_init);
    RUN_TEST(test_hal_adapter_rejects_init_before_configure_and_duplicate_configure);
    RUN_TEST(test_hal_adapter_exposes_name_resolution_and_board_count);
    RUN_TEST(test_do_set_updates_stats_and_rejects_invalid_pin);
    RUN_TEST(test_di_test_override_controls_read_value);
    RUN_TEST(test_wait_boards_online_uses_sdk_probe);
    RUN_TEST(test_pulse_read_and_clear_delegate_to_sdk);
    RUN_TEST(test_adc_read_delegates_to_sdk);

    return UNITY_END();
}
