/**
 * @file    test_drv_vfd.c
 * @brief   drv_vfd 单元测试
 *
 * 分组：
 *   A. 参数校验
 *   B. 纯逻辑 / get_state
 *   C. IO 引脚控制
 *   D. read / write 统一接口
 *   E. 初始化异常
 *   F. 运行时边界
 *   G. set_rst 引脚原语
 *
 * 周期采样、缓存与事件上报用例见后续 test_vfd_comm_monitor。
 */

#include "framework/adapters/outbound/hal/linux_hw/drv/drv_vfd.h"
#include "tests/support/modbus_stub.h"
#include "unity.h"

#include <string.h>

#define P_FWD  IO_DO(1, 1)
#define P_REV  IO_DO(1, 2)
#define P_RST  IO_DO(1, 3)
#define P_SPD1 IO_DO(1, 4)
#define P_SPD2 IO_DO(1, 5)

static const uint8_t k_spd_cfg[VFD_GEAR_MAX] = {
    VFD_SPD_IO(1, 0),
    VFD_SPD_IO(0, 1),
    VFD_SPD_IO(1, 1),
};

#define PIN_LOG_MAX 64
typedef struct { uint16_t raw; bool val; } pin_evt_t;
static pin_evt_t s_pin_log[PIN_LOG_MAX];
static int       s_pin_log_n = 0;

static sw_err_t fake_do_set(io_do_t pin, bool val)
{
    if (s_pin_log_n < PIN_LOG_MAX) {
        s_pin_log[s_pin_log_n].raw = pin.raw;
        s_pin_log[s_pin_log_n].val = val;
        s_pin_log_n++;
    }
    return SW_OK;
}

static void pin_log_reset(void) { s_pin_log_n = 0; }

static bool pin_last_val(uint16_t raw, bool *out)
{
    for (int i = s_pin_log_n - 1; i >= 0; i--) {
        if (s_pin_log[i].raw == raw) {
            if (out) {
                *out = s_pin_log[i].val;
            }
            return true;
        }
    }
    return false;
}

static drv_vfd_t g_vfd;
static bool      g_vfd_ready = false;

static void ensure_vfd_ready(void)
{
    if (g_vfd_ready) {
        return;
    }
    modbus_stub_reset();
    TEST_ASSERT_EQUAL_INT(SW_OK, drv_vfd_init(&g_vfd, "/dev/ttyS0", 9600, 1,
                                               P_FWD, P_REV, P_RST, fake_do_set));
    TEST_ASSERT_EQUAL_INT(SW_OK, drv_vfd_config_speed_io(&g_vfd, P_SPD1, P_SPD2, k_spd_cfg));
    g_vfd_ready = true;
}

void setUp(void)
{
    modbus_stub_reset();
    pin_log_reset();
    ensure_vfd_ready();
    g_vfd.gear            = VFD_GEAR_STOP;
    g_vfd.comm_fail_count = 0U;
    g_vfd.mb_connected    = true;
    g_vfd.pin_rev         = P_REV;
    g_vfd.pin_rst         = P_RST;
}

void tearDown(void) {}

static void test_init_null_vfd(void)
{
    TEST_ASSERT_EQUAL_INT(SW_ERR_PARAM,
        drv_vfd_init(NULL, "/dev/ttyS0", 9600, 1, P_FWD, P_REV, P_RST, fake_do_set));
}

static void test_init_null_port(void)
{
    drv_vfd_t tmp;
    TEST_ASSERT_EQUAL_INT(SW_ERR_PARAM,
        drv_vfd_init(&tmp, NULL, 9600, 1, P_FWD, P_REV, P_RST, fake_do_set));
}

static void test_init_null_do_set(void)
{
    drv_vfd_t tmp;
    TEST_ASSERT_EQUAL_INT(SW_ERR_PARAM,
        drv_vfd_init(&tmp, "/dev/ttyS0", 9600, 1, P_FWD, P_REV, P_RST, NULL));
}

static void test_config_null_cfg(void)
{
    TEST_ASSERT_EQUAL_INT(SW_ERR_PARAM,
        drv_vfd_config_speed_io(&g_vfd, P_SPD1, P_SPD2, NULL));
}

static void test_config_null_pin(void)
{
    io_do_t null_pin = {IO_HANDLE_NULL};
    TEST_ASSERT_EQUAL_INT(SW_ERR_PARAM,
        drv_vfd_config_speed_io(&g_vfd, null_pin, P_SPD2, k_spd_cfg));
}

static void test_config_zero_mapping(void)
{
    uint8_t bad[VFD_GEAR_MAX] = {VFD_SPD_IO(1, 0), 0x00U, VFD_SPD_IO(1, 1)};
    TEST_ASSERT_EQUAL_INT(SW_ERR_PARAM,
        drv_vfd_config_speed_io(&g_vfd, P_SPD1, P_SPD2, bad));
}

static void test_apply_gear_too_large(void)
{
    TEST_ASSERT_EQUAL_INT(SW_ERR_PARAM,
        drv_vfd_apply_gear(&g_vfd, (drv_vfd_gear_t)(VFD_GEAR_MAX + 1)));
}

static void test_apply_gear_too_small(void)
{
    TEST_ASSERT_EQUAL_INT(SW_ERR_PARAM,
        drv_vfd_apply_gear(&g_vfd, (drv_vfd_gear_t)(-(VFD_GEAR_MAX + 1))));
}

static void test_apply_gear_reverse_no_rev_pin(void)
{
    g_vfd.pin_rev = (io_do_t){IO_HANDLE_NULL};
    TEST_ASSERT_EQUAL_INT(SW_ERR_PARAM, drv_vfd_apply_gear(&g_vfd, VFD_GEAR_REV_1));
}

static void test_apply_gear_uninit_returns_not_init(void)
{
    drv_vfd_t tmp;
    (void)memset(&tmp, 0, sizeof(tmp));
    TEST_ASSERT_EQUAL_INT(SW_ERR_NOT_INIT, drv_vfd_apply_gear(&tmp, VFD_GEAR_FWD_1));
}

static void test_stop_outputs_uninit_returns_not_init(void)
{
    drv_vfd_t tmp;
    (void)memset(&tmp, 0, sizeof(tmp));
    TEST_ASSERT_EQUAL_INT(SW_ERR_NOT_INIT, drv_vfd_stop_outputs(&tmp));
}

static void test_write_uninit_returns_not_init(void)
{
    drv_vfd_t tmp;
    (void)memset(&tmp, 0, sizeof(tmp));
    TEST_ASSERT_EQUAL_INT(SW_ERR_NOT_INIT,
        drv_vfd_write(&tmp, DRV_VFD_REG_CLEAR_FAULT, 0U));
}

static void test_read_uninit_returns_not_init(void)
{
    drv_vfd_t tmp;
    uint16_t val;
    (void)memset(&tmp, 0, sizeof(tmp));
    TEST_ASSERT_EQUAL_INT(SW_ERR_NOT_INIT,
        drv_vfd_read(&tmp, DRV_VFD_REG_CURRENT, &val));
}

static void test_set_rst_uninit_returns_not_init(void)
{
    drv_vfd_t tmp;
    (void)memset(&tmp, 0, sizeof(tmp));
    TEST_ASSERT_EQUAL_INT(SW_ERR_NOT_INIT, drv_vfd_set_rst(&tmp, true));
}

static void test_get_state_null(void)
{
    TEST_ASSERT_EQUAL_INT(HAL_VFD_STATE_STOPPED, drv_vfd_get_state(NULL));
}

static void test_get_state_fwd_after_apply_gear(void)
{
    drv_vfd_apply_gear(&g_vfd, VFD_GEAR_FWD_1);
    TEST_ASSERT_EQUAL_INT(HAL_VFD_STATE_FWD, drv_vfd_get_state(&g_vfd));
}

static void test_get_state_rev_after_apply_gear(void)
{
    drv_vfd_apply_gear(&g_vfd, VFD_GEAR_REV_1);
    TEST_ASSERT_EQUAL_INT(HAL_VFD_STATE_REV, drv_vfd_get_state(&g_vfd));
}

static void test_get_state_stop_after_stop_outputs(void)
{
    drv_vfd_apply_gear(&g_vfd, VFD_GEAR_FWD_1);
    drv_vfd_stop_outputs(&g_vfd);
    TEST_ASSERT_EQUAL_INT(HAL_VFD_STATE_STOPPED, drv_vfd_get_state(&g_vfd));
}

static void test_apply_gear_fwd1_sets_correct_pins(void)
{
    bool v;
    drv_vfd_apply_gear(&g_vfd, VFD_GEAR_FWD_1);
    TEST_ASSERT_TRUE(pin_last_val(P_FWD.raw, &v));  TEST_ASSERT_TRUE(v);
    TEST_ASSERT_TRUE(pin_last_val(P_REV.raw, &v));  TEST_ASSERT_FALSE(v);
    TEST_ASSERT_TRUE(pin_last_val(P_SPD1.raw, &v)); TEST_ASSERT_TRUE(v);
    TEST_ASSERT_TRUE(pin_last_val(P_SPD2.raw, &v)); TEST_ASSERT_FALSE(v);
}

static void test_apply_gear_rev1_sets_correct_pins(void)
{
    bool v;
    drv_vfd_apply_gear(&g_vfd, VFD_GEAR_REV_1);
    TEST_ASSERT_TRUE(pin_last_val(P_REV.raw, &v)); TEST_ASSERT_TRUE(v);
    TEST_ASSERT_TRUE(pin_last_val(P_FWD.raw, &v)); TEST_ASSERT_FALSE(v);
}

static void test_stop_outputs_clears_all_pins(void)
{
    bool v;
    drv_vfd_apply_gear(&g_vfd, VFD_GEAR_FWD_2);
    pin_log_reset();
    drv_vfd_stop_outputs(&g_vfd);
    TEST_ASSERT_TRUE(pin_last_val(P_FWD.raw,  &v)); TEST_ASSERT_FALSE(v);
    TEST_ASSERT_TRUE(pin_last_val(P_SPD1.raw, &v)); TEST_ASSERT_FALSE(v);
    TEST_ASSERT_TRUE(pin_last_val(P_SPD2.raw, &v)); TEST_ASSERT_FALSE(v);
}

static void test_apply_gear_fwd_to_rev_immediate(void)
{
    bool v;
    drv_vfd_apply_gear(&g_vfd, VFD_GEAR_FWD_1);
    pin_log_reset();
    drv_vfd_apply_gear(&g_vfd, VFD_GEAR_REV_1);
    TEST_ASSERT_EQUAL_INT(HAL_VFD_STATE_REV, drv_vfd_get_state(&g_vfd));
    TEST_ASSERT_TRUE(pin_last_val(P_REV.raw, &v)); TEST_ASSERT_TRUE(v);
    TEST_ASSERT_TRUE(pin_last_val(P_FWD.raw, &v)); TEST_ASSERT_FALSE(v);
}

static void test_read_fault_code_success(void)
{
    uint16_t val = 0U;
    modbus_stub_set_read_result(1, 0x0005U);
    TEST_ASSERT_EQUAL_INT(SW_OK, drv_vfd_read(&g_vfd, DRV_VFD_REG_FAULT_CODE, &val));
    TEST_ASSERT_EQUAL_UINT16(0x0005U, val);
}

static void test_read_fault_code_null_out(void)
{
    TEST_ASSERT_EQUAL_INT(SW_ERR_PARAM,
        drv_vfd_read(&g_vfd, DRV_VFD_REG_FAULT_CODE, NULL));
}

static void test_read_current_success(void)
{
    uint16_t cur = 0U;
    modbus_stub_set_read_result(1, 150U);
    TEST_ASSERT_EQUAL_INT(SW_OK, drv_vfd_read(&g_vfd, DRV_VFD_REG_CURRENT, &cur));
    TEST_ASSERT_EQUAL_UINT16(150U, cur);
}

static void test_read_current_null_out(void)
{
    TEST_ASSERT_EQUAL_INT(SW_ERR_PARAM,
        drv_vfd_read(&g_vfd, DRV_VFD_REG_CURRENT, NULL));
}

static void test_read_current_comm_fail(void)
{
    uint16_t cur = 0U;
    modbus_stub_set_read_result(-1, 0U);
    TEST_ASSERT_EQUAL_INT(SW_ERR_COMM, drv_vfd_read(&g_vfd, DRV_VFD_REG_CURRENT, &cur));
}

static void test_read_state_success(void)
{
    uint16_t st = 0U;
    modbus_stub_set_read_result(1, 0x0003U);
    TEST_ASSERT_EQUAL_INT(SW_OK, drv_vfd_read(&g_vfd, DRV_VFD_REG_STATE, &st));
    TEST_ASSERT_EQUAL_UINT16(0x0003U, st);
}

static void test_read_state_null_out(void)
{
    TEST_ASSERT_EQUAL_INT(SW_ERR_PARAM,
        drv_vfd_read(&g_vfd, DRV_VFD_REG_STATE, NULL));
}

static void test_read_writeonly_reg_returns_param(void)
{
    uint16_t val;
    TEST_ASSERT_EQUAL_INT(SW_ERR_PARAM, drv_vfd_read(&g_vfd, DRV_VFD_REG_FREQ, &val));
    TEST_ASSERT_EQUAL_INT(SW_ERR_PARAM, drv_vfd_read(&g_vfd, DRV_VFD_REG_CLEAR_FAULT, &val));
}

static void test_write_freq_not_supported_for_vendor(void)
{
    TEST_ASSERT_EQUAL_INT(SW_ERR_PARAM,
        drv_vfd_write(&g_vfd, DRV_VFD_REG_FREQ, 50U));
}

static void test_write_readonly_reg_returns_param(void)
{
    TEST_ASSERT_EQUAL_INT(SW_ERR_PARAM, drv_vfd_write(&g_vfd, DRV_VFD_REG_STATE, 0U));
    TEST_ASSERT_EQUAL_INT(SW_ERR_PARAM, drv_vfd_write(&g_vfd, DRV_VFD_REG_FAULT_CODE, 0U));
    TEST_ASSERT_EQUAL_INT(SW_ERR_PARAM, drv_vfd_write(&g_vfd, DRV_VFD_REG_CURRENT, 0U));
}

static void test_write_clear_fault_correct_reg_and_data(void)
{
    TEST_ASSERT_EQUAL_INT(SW_OK, drv_vfd_write(&g_vfd, DRV_VFD_REG_CLEAR_FAULT, 0U));
    TEST_ASSERT_EQUAL_INT(0x1000, modbus_stub_last_write_reg());
    TEST_ASSERT_EQUAL_UINT16(0x1101U, modbus_stub_last_write_val());
}

static void test_write_clear_fault_comm_fail(void)
{
    modbus_stub_set_write_result(-1);
    TEST_ASSERT_EQUAL_INT(SW_ERR_COMM,
        drv_vfd_write(&g_vfd, DRV_VFD_REG_CLEAR_FAULT, 0U));
}

static void test_comm_fail_count_resets_at_reconnect_threshold(void)
{
    uint16_t dummy;
    unsigned i;

    /* 与 drv_vfd.c 中 VFD_COMM_FAIL_RECONNECT 保持一致 */
    enum { k_reconnect_threshold = 50U };

    g_vfd.comm_fail_count = 0U;
    g_vfd.mb_connected    = true;
    modbus_stub_set_read_result(-1, 0U);
    for (i = 0U; i < k_reconnect_threshold; i++) {
        (void)drv_vfd_read(&g_vfd, DRV_VFD_REG_CURRENT, &dummy);
    }
    TEST_ASSERT_EQUAL_UINT16(0U, g_vfd.comm_fail_count);
}

static void test_init_modbus_ctx_fail(void)
{
    drv_vfd_t tmp;
    modbus_stub_set_new_rtu_fail(true);
    TEST_ASSERT_EQUAL_INT(SW_ERR_HW,
        drv_vfd_init(&tmp, "/dev/ttyS0", 9600, 2, P_FWD, P_REV, P_RST, fake_do_set));
    TEST_ASSERT_NULL(tmp.serial_port);
}

static void test_apply_gear_stop_when_already_stopped(void)
{
    TEST_ASSERT_EQUAL_INT(SW_OK, drv_vfd_apply_gear(&g_vfd, VFD_GEAR_STOP));
    TEST_ASSERT_EQUAL_INT(HAL_VFD_STATE_STOPPED, drv_vfd_get_state(&g_vfd));
}

static void test_set_rst_high(void)
{
    bool v;
    TEST_ASSERT_EQUAL_INT(SW_OK, drv_vfd_set_rst(&g_vfd, true));
    TEST_ASSERT_TRUE(pin_last_val(P_RST.raw, &v));
    TEST_ASSERT_TRUE(v);
}

static void test_set_rst_low(void)
{
    bool v;
    drv_vfd_set_rst(&g_vfd, true);
    pin_log_reset();
    TEST_ASSERT_EQUAL_INT(SW_OK, drv_vfd_set_rst(&g_vfd, false));
    TEST_ASSERT_TRUE(pin_last_val(P_RST.raw, &v));
    TEST_ASSERT_FALSE(v);
}

static void test_set_rst_null_pin_returns_param(void)
{
    g_vfd.pin_rst = (io_do_t){IO_HANDLE_NULL};
    TEST_ASSERT_EQUAL_INT(SW_ERR_PARAM, drv_vfd_set_rst(&g_vfd, true));
}

int main(void)
{
    UNITY_BEGIN();

    RUN_TEST(test_init_null_vfd);
    RUN_TEST(test_init_null_port);
    RUN_TEST(test_init_null_do_set);
    RUN_TEST(test_config_null_cfg);
    RUN_TEST(test_config_null_pin);
    RUN_TEST(test_config_zero_mapping);
    RUN_TEST(test_apply_gear_too_large);
    RUN_TEST(test_apply_gear_too_small);
    RUN_TEST(test_apply_gear_reverse_no_rev_pin);
    RUN_TEST(test_apply_gear_uninit_returns_not_init);
    RUN_TEST(test_stop_outputs_uninit_returns_not_init);
    RUN_TEST(test_write_uninit_returns_not_init);
    RUN_TEST(test_read_uninit_returns_not_init);
    RUN_TEST(test_set_rst_uninit_returns_not_init);

    RUN_TEST(test_get_state_null);
    RUN_TEST(test_get_state_fwd_after_apply_gear);
    RUN_TEST(test_get_state_rev_after_apply_gear);
    RUN_TEST(test_get_state_stop_after_stop_outputs);

    RUN_TEST(test_apply_gear_fwd1_sets_correct_pins);
    RUN_TEST(test_apply_gear_rev1_sets_correct_pins);
    RUN_TEST(test_stop_outputs_clears_all_pins);
    RUN_TEST(test_apply_gear_fwd_to_rev_immediate);

    RUN_TEST(test_read_fault_code_success);
    RUN_TEST(test_read_fault_code_null_out);
    RUN_TEST(test_read_current_success);
    RUN_TEST(test_read_current_null_out);
    RUN_TEST(test_read_current_comm_fail);
    RUN_TEST(test_read_state_success);
    RUN_TEST(test_read_state_null_out);
    RUN_TEST(test_read_writeonly_reg_returns_param);
    RUN_TEST(test_write_freq_not_supported_for_vendor);
    RUN_TEST(test_write_readonly_reg_returns_param);
    RUN_TEST(test_write_clear_fault_correct_reg_and_data);
    RUN_TEST(test_write_clear_fault_comm_fail);
    RUN_TEST(test_comm_fail_count_resets_at_reconnect_threshold);

    RUN_TEST(test_init_modbus_ctx_fail);

    RUN_TEST(test_apply_gear_stop_when_already_stopped);

    RUN_TEST(test_set_rst_high);
    RUN_TEST(test_set_rst_low);
    RUN_TEST(test_set_rst_null_pin_returns_param);

    return UNITY_END();
}
