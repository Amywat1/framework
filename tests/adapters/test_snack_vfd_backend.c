/**
 * @file    test_snack_vfd_backend.c
 * @brief   Snack VFD backend 单元测试。
 */

#include "adapters/outbound/hal/providers/snack/io_exp/io_exp_driver.h"
#include "adapters/outbound/hal/providers/snack/modbus/snack_vfd_backend.h"
#include "common/io_handle.h"
#include "ports/outbound/hal/hal_vfd_port.h"
#include "tests/stubs/snack/drv_modbus_link_fake.h"
#include "unity.h"

#include <string.h>

#define TEST_VFD_ID 0

static int      s_events[8];
static unsigned s_event_count;

static void event_cb(int event_code)
{
    if (s_event_count < (sizeof(s_events) / sizeof(s_events[0]))) {
        s_events[s_event_count++] = event_code;
    }
}

static const hal_vfd_ops_t *vfd_ops(void)
{
    const hal_vfd_ops_t *ops = hal_vfd_get_ops();

    TEST_ASSERT_NOT_NULL(ops);
    return ops;
}

void hal_vfd_manager_test_reset(void);

static void init_io_driver(void)
{
    drv_io_cfg_t cfg = {
        .can_bus     = "can0",
        .can_baud    = 500000,
        .self_node   = 9,
        .board_count = 2,
        .pin_count   = 8,
        .di_table    = NULL,
        .di_count    = 0,
        .do_table    = NULL,
        .do_count    = 0,
    };

    TEST_ASSERT_EQUAL_INT(SW_OK, drv_io_init(&cfg));
}

static snack_vfd_backend_instance_cfg_t make_cfg(void)
{
    snack_vfd_backend_instance_cfg_t cfg;

    memset(&cfg, 0, sizeof(cfg));
    cfg.serial_port      = "/dev/ttyS2";
    cfg.baud             = 19200;
    cfg.modbus_addr      = 3;
    cfg.pin_fwd          = IO_DO(1U, 1U);
    cfg.pin_rev          = IO_DO(1U, 2U);
    cfg.pin_rst          = IO_DO(1U, 3U);
    cfg.speed_io_enabled = true;
    cfg.pin_spd1         = IO_DO(1U, 4U);
    cfg.pin_spd2         = IO_DO(1U, 5U);
    cfg.speed_io[0]      = SNACK_VFD_BACKEND_SPEED_IO(true, false);
    cfg.speed_io[1]      = SNACK_VFD_BACKEND_SPEED_IO(false, true);
    cfg.speed_io[2]      = SNACK_VFD_BACKEND_SPEED_IO(true, true);
    cfg.monitor_mask     = HAL_VFD_MON_NONE;
    return cfg;
}

void setUp(void)
{
    snack_modbus_fake_reset();
    memset(s_events, 0, sizeof(s_events));
    s_event_count = 0;
    hal_vfd_manager_test_reset();
    snack_vfd_backend_test_reset();
    init_io_driver();
    snack_vfd_backend_register();
}

void tearDown(void)
{
}

static void configure_bind_init(hal_vfd_id_t id, const snack_vfd_backend_instance_cfg_t *cfg)
{
    TEST_ASSERT_EQUAL_INT(SW_OK, snack_vfd_backend_instance_configure(id, cfg));
    TEST_ASSERT_EQUAL_INT(SW_OK, snack_vfd_backend_instance_bind(id));
    TEST_ASSERT_EQUAL_INT(SW_OK, vfd_ops()->init());
}

static void test_instance_configure_rejects_invalid_config(void)
{
    snack_vfd_backend_instance_cfg_t cfg = make_cfg();

    TEST_ASSERT_EQUAL_INT(SW_ERR_PARAM, snack_vfd_backend_instance_configure(-1, &cfg));
    TEST_ASSERT_EQUAL_INT(SW_ERR_PARAM, snack_vfd_backend_instance_configure(8, &cfg));
    TEST_ASSERT_EQUAL_INT(SW_ERR_PARAM, snack_vfd_backend_instance_configure(TEST_VFD_ID, NULL));
}

static void test_instance_configure_bind_and_hal_init_pass_modbus_parameters(void)
{
    snack_vfd_backend_instance_cfg_t cfg = make_cfg();

    TEST_ASSERT_EQUAL_INT(SW_OK, snack_vfd_backend_instance_configure(TEST_VFD_ID, &cfg));
    TEST_ASSERT_EQUAL_INT(SW_ERR_BUSY, snack_vfd_backend_instance_configure(TEST_VFD_ID, &cfg));
    TEST_ASSERT_EQUAL_INT(SW_OK, snack_vfd_backend_instance_bind(TEST_VFD_ID));
    TEST_ASSERT_EQUAL_INT(SW_ERR_BUSY, snack_vfd_backend_instance_bind(TEST_VFD_ID));
    TEST_ASSERT_EQUAL_INT(SW_OK, vfd_ops()->init());
    TEST_ASSERT_TRUE(snack_modbus_fake_init_called());
    TEST_ASSERT_EQUAL_STRING("/dev/ttyS2", snack_modbus_fake_serial_port());
    TEST_ASSERT_EQUAL_INT(19200, snack_modbus_fake_baud());
    TEST_ASSERT_EQUAL_INT(3, snack_modbus_fake_addr());
}

static void test_run_stop_and_state_use_io_backend(void)
{
    snack_vfd_backend_instance_cfg_t cfg = make_cfg();

    configure_bind_init(TEST_VFD_ID, &cfg);
    TEST_ASSERT_EQUAL_INT(SW_OK, vfd_ops()->run(TEST_VFD_ID, 2));
    TEST_ASSERT_EQUAL_INT(HAL_VFD_STATE_FWD, vfd_ops()->get_state(TEST_VFD_ID));

    TEST_ASSERT_EQUAL_INT(SW_OK, vfd_ops()->run(TEST_VFD_ID, -1));
    TEST_ASSERT_EQUAL_INT(HAL_VFD_STATE_REV, vfd_ops()->get_state(TEST_VFD_ID));

    TEST_ASSERT_EQUAL_INT(SW_OK, vfd_ops()->stop(TEST_VFD_ID));
    TEST_ASSERT_EQUAL_INT(HAL_VFD_STATE_STOPPED, vfd_ops()->get_state(TEST_VFD_ID));
}

static void test_run_rejects_invalid_gear_and_unsupported_reverse(void)
{
    snack_vfd_backend_instance_cfg_t cfg = make_cfg();

    configure_bind_init(TEST_VFD_ID, &cfg);
    TEST_ASSERT_EQUAL_INT(SW_ERR_PARAM, vfd_ops()->run(TEST_VFD_ID, 4));

    cfg         = make_cfg();
    cfg.pin_rev = (io_do_t){IO_HANDLE_NULL};
    TEST_ASSERT_EQUAL_INT(SW_OK, snack_vfd_backend_instance_configure(1, &cfg));
    TEST_ASSERT_EQUAL_INT(SW_OK, snack_vfd_backend_instance_bind(1));
    TEST_ASSERT_EQUAL_INT(SW_OK, vfd_ops()->init());
    TEST_ASSERT_EQUAL_INT(SW_ERR_PARAM, vfd_ops()->run(1, -1));
}

static void test_set_freq_is_not_supported_for_current_vendor(void)
{
    snack_vfd_backend_instance_cfg_t cfg = make_cfg();

    configure_bind_init(TEST_VFD_ID, &cfg);
    TEST_ASSERT_EQUAL_INT(SW_ERR_PARAM, vfd_ops()->set_freq(TEST_VFD_ID, 50U));
}

static void test_read_and_clear_fault_delegate_to_modbus(void)
{
    uint16_t                         val = 0;
    snack_vfd_backend_instance_cfg_t cfg = make_cfg();

    cfg.pin_rst = (io_do_t){IO_HANDLE_NULL};
    configure_bind_init(TEST_VFD_ID, &cfg);
    snack_modbus_fake_set_read_value(0x1007U, 0x0022U);
    TEST_ASSERT_EQUAL_INT(SW_OK, vfd_ops()->read(TEST_VFD_ID, HAL_VFD_REG_FAULT_CODE, &val));
    TEST_ASSERT_EQUAL_UINT16(0x0022U, val);
    TEST_ASSERT_EQUAL_UINT16(0x1007U, snack_modbus_fake_last_read_addr());

    TEST_ASSERT_EQUAL_INT(SW_OK, vfd_ops()->fault_reset(TEST_VFD_ID));
    TEST_ASSERT_EQUAL_UINT16(0x1000U, snack_modbus_fake_last_write_addr());
    TEST_ASSERT_EQUAL_UINT16(0x1101U, snack_modbus_fake_last_write_val());
}

static void test_fault_reset_uses_rst_pin_when_modbus_clear_not_available(void)
{
    snack_vfd_backend_instance_cfg_t cfg = make_cfg();
    unsigned                         before;

    cfg.pin_rst = IO_DO(2U, 1U);
    configure_bind_init(TEST_VFD_ID, &cfg);
    before = snack_modbus_fake_write_count();
    TEST_ASSERT_EQUAL_INT(SW_OK, vfd_ops()->fault_reset(TEST_VFD_ID));
    TEST_ASSERT_EQUAL_UINT(before, snack_modbus_fake_write_count());
}

static void test_monitor_mask_can_be_updated_after_init(void)
{
    snack_vfd_backend_instance_cfg_t cfg = make_cfg();

    TEST_ASSERT_EQUAL_INT(SW_ERR_NOT_INIT, snack_vfd_backend_instance_set_monitor_mask(7, HAL_VFD_MON_FAULT));

    TEST_ASSERT_EQUAL_INT(SW_OK, snack_vfd_backend_instance_configure(TEST_VFD_ID, &cfg));
    TEST_ASSERT_EQUAL_INT(SW_OK, snack_vfd_backend_instance_bind(TEST_VFD_ID));
    vfd_ops()->register_event_cb(TEST_VFD_ID, event_cb);
    TEST_ASSERT_EQUAL_INT(SW_OK, snack_vfd_backend_instance_set_monitor_mask(TEST_VFD_ID, HAL_VFD_MON_FAULT));
}

static void test_bound_but_not_hal_inited_operations_return_not_init(void)
{
    snack_vfd_backend_instance_cfg_t cfg = make_cfg();

    TEST_ASSERT_EQUAL_INT(SW_OK, snack_vfd_backend_instance_configure(TEST_VFD_ID, &cfg));
    TEST_ASSERT_EQUAL_INT(SW_OK, snack_vfd_backend_instance_bind(TEST_VFD_ID));
    TEST_ASSERT_EQUAL_INT(SW_ERR_NOT_INIT, vfd_ops()->run(TEST_VFD_ID, 1));
}

int main(void)
{
    UNITY_BEGIN();

    RUN_TEST(test_instance_configure_rejects_invalid_config);
    RUN_TEST(test_instance_configure_bind_and_hal_init_pass_modbus_parameters);
    RUN_TEST(test_bound_but_not_hal_inited_operations_return_not_init);
    RUN_TEST(test_run_stop_and_state_use_io_backend);
    RUN_TEST(test_run_rejects_invalid_gear_and_unsupported_reverse);
    RUN_TEST(test_set_freq_is_not_supported_for_current_vendor);
    RUN_TEST(test_read_and_clear_fault_delegate_to_modbus);
    RUN_TEST(test_fault_reset_uses_rst_pin_when_modbus_clear_not_available);
    RUN_TEST(test_monitor_mask_can_be_updated_after_init);

    return UNITY_END();
}
