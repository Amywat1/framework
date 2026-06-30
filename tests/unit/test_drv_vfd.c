/**
 * @file    test_drv_vfd.c
 * @brief   drv_vfd 单元测试
 *
 * 分组：
 *   A. 参数校验
 *   B. 纯逻辑 / get_state
 *   C. IO 引脚控制
 *   D. Modbus 操作与通信事件
 *   E. 初始化异常
 *   F. 运行时边界与意外情况
 *   G. 未覆盖接口（read_current / read_status / set_monitor_mask）
 *
 * 设计约束：
 *   drv_vfd_init 的 s_bus_ports / s_monitor_list 无法跨测试重置，
 *   全文件只初始化一次全局 VFD（g_vfd），setUp 中将其归零到停止态。
 *   初始化后立即设置 DRV_VFD_MON_NONE，防止后台 Modbus 干扰 stub 计数。
 */

#include "adapters/hal/linux_hw/drv/drv_vfd.h"
#include "tests/support/modbus_stub.h"
#include "unity.h"

#include <string.h>

/* -------------------------------------------------------------------------
 * 引脚句柄（子板 1 的 DO）
 * ------------------------------------------------------------------------- */
#define P_FWD  IO_DO(1, 1)
#define P_REV  IO_DO(1, 2)
#define P_RST  IO_DO(1, 3)
#define P_SPD1 IO_DO(1, 4)
#define P_SPD2 IO_DO(1, 5)

/* 速度挡位：gear1=spd1H/spd2L, gear2=spd1L/spd2H, gear3=spd1H/spd2H */
static const uint8_t k_spd_cfg[VFD_GEAR_MAX] = {
    VFD_SPD_IO(1, 0),
    VFD_SPD_IO(0, 1),
    VFD_SPD_IO(1, 1),
};

/* -------------------------------------------------------------------------
 * 引脚日志
 * ------------------------------------------------------------------------- */
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
            if (out) *out = s_pin_log[i].val;
            return true;
        }
    }
    return false;
}

/* -------------------------------------------------------------------------
 * 事件回调
 * ------------------------------------------------------------------------- */
static volatile int s_last_event = 0;
static void event_cb(int code) { s_last_event = code; }

/* -------------------------------------------------------------------------
 * 全局 VFD（整个文件只初始化一次）
 * ------------------------------------------------------------------------- */
static drv_vfd_t g_vfd;
static bool      g_vfd_ready = false;

static void ensure_vfd_ready(void)
{
    if (g_vfd_ready) return;
    modbus_stub_reset();
    TEST_ASSERT_EQUAL_INT(SW_OK, drv_vfd_init(&g_vfd, "/dev/ttyS0", 9600, 1,
                                               P_FWD, P_REV, P_RST, fake_do_set));
    drv_vfd_set_monitor_mask(&g_vfd, DRV_VFD_MON_NONE);
    TEST_ASSERT_EQUAL_INT(SW_OK, drv_vfd_config_speed_io(&g_vfd, P_SPD1, P_SPD2, k_spd_cfg));
    drv_vfd_register_event_cb(&g_vfd, event_cb);
    g_vfd_ready = true;
}

void setUp(void)
{
    modbus_stub_reset();
    pin_log_reset();
    s_last_event = 0;
    ensure_vfd_ready();
    g_vfd.gear            = VFD_GEAR_STOP;
    g_vfd.pending_gear    = VFD_GEAR_STOP;
    g_vfd.rst_active      = false;
    g_vfd.comm_ok         = true;
    g_vfd.comm_fail_count = 0U;
    g_vfd.mb_connected    = true;
    g_vfd.pin_rev         = P_REV; /* 防止某些测试修改后影响后续 */
    drv_vfd_register_event_cb(&g_vfd, event_cb);
}

void tearDown(void) {}

/* =========================================================================
 * A. 参数校验
 * ========================================================================= */

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

static void test_run_gear_too_large(void)
{
    TEST_ASSERT_EQUAL_INT(SW_ERR_PARAM,
        drv_vfd_run(&g_vfd, (drv_vfd_gear_t)(VFD_GEAR_MAX + 1)));
}

static void test_run_gear_too_small(void)
{
    TEST_ASSERT_EQUAL_INT(SW_ERR_PARAM,
        drv_vfd_run(&g_vfd, (drv_vfd_gear_t)(-(VFD_GEAR_MAX + 1))));
}

static void test_run_reverse_no_rev_pin(void)
{
    g_vfd.pin_rev = (io_do_t){IO_HANDLE_NULL};
    TEST_ASSERT_EQUAL_INT(SW_ERR_PARAM, drv_vfd_run(&g_vfd, VFD_GEAR_REV_1));
}

static void test_run_uninit_returns_not_init(void)
{
    drv_vfd_t tmp; memset(&tmp, 0, sizeof(tmp));
    TEST_ASSERT_EQUAL_INT(SW_ERR_NOT_INIT, drv_vfd_run(&tmp, VFD_GEAR_FWD_1));
}

static void test_set_freq_uninit_returns_not_init(void)
{
    drv_vfd_t tmp; memset(&tmp, 0, sizeof(tmp));
    TEST_ASSERT_EQUAL_INT(SW_ERR_NOT_INIT, drv_vfd_set_freq(&tmp, 50U));
}

static void test_fault_reset_uninit_returns_not_init(void)
{
    drv_vfd_t tmp; memset(&tmp, 0, sizeof(tmp));
    TEST_ASSERT_EQUAL_INT(SW_ERR_NOT_INIT, drv_vfd_fault_reset(&tmp));
}

/* =========================================================================
 * B. 纯逻辑 / get_state
 * ========================================================================= */

static void test_get_state_null(void)
{
    TEST_ASSERT_EQUAL_INT(HAL_VFD_STATE_STOPPED, drv_vfd_get_state(NULL));
}

static void test_get_cached_fault_null(void)
{
    TEST_ASSERT_EQUAL_UINT16(0U, drv_vfd_get_cached_fault_code(NULL));
}

static void test_get_cached_current_null(void)
{
    TEST_ASSERT_EQUAL_UINT16(0U, drv_vfd_get_cached_current(NULL));
}

static void test_get_state_fwd_after_run(void)
{
    drv_vfd_run(&g_vfd, VFD_GEAR_FWD_1);
    TEST_ASSERT_EQUAL_INT(HAL_VFD_STATE_FWD, drv_vfd_get_state(&g_vfd));
}

static void test_get_state_rev_after_run(void)
{
    drv_vfd_run(&g_vfd, VFD_GEAR_REV_1);
    TEST_ASSERT_EQUAL_INT(HAL_VFD_STATE_REV, drv_vfd_get_state(&g_vfd));
}

static void test_get_state_stop_after_run(void)
{
    drv_vfd_run(&g_vfd, VFD_GEAR_FWD_1);
    drv_vfd_run(&g_vfd, VFD_GEAR_STOP);
    TEST_ASSERT_EQUAL_INT(HAL_VFD_STATE_STOPPED, drv_vfd_get_state(&g_vfd));
}

/* =========================================================================
 * C. IO 引脚控制
 * ========================================================================= */

static void test_run_fwd1_sets_correct_pins(void)
{
    bool v;
    drv_vfd_run(&g_vfd, VFD_GEAR_FWD_1);
    TEST_ASSERT_TRUE(pin_last_val(P_FWD.raw, &v));  TEST_ASSERT_TRUE(v);
    TEST_ASSERT_TRUE(pin_last_val(P_REV.raw, &v));  TEST_ASSERT_FALSE(v);
    TEST_ASSERT_TRUE(pin_last_val(P_SPD1.raw, &v)); TEST_ASSERT_TRUE(v);  /* SPD_IO(1,0) */
    TEST_ASSERT_TRUE(pin_last_val(P_SPD2.raw, &v)); TEST_ASSERT_FALSE(v);
}

static void test_run_rev1_sets_correct_pins(void)
{
    bool v;
    drv_vfd_run(&g_vfd, VFD_GEAR_REV_1);
    TEST_ASSERT_TRUE(pin_last_val(P_REV.raw, &v)); TEST_ASSERT_TRUE(v);
    TEST_ASSERT_TRUE(pin_last_val(P_FWD.raw, &v)); TEST_ASSERT_FALSE(v);
}

static void test_run_stop_clears_all_pins(void)
{
    bool v;
    drv_vfd_run(&g_vfd, VFD_GEAR_FWD_2);
    pin_log_reset();
    drv_vfd_run(&g_vfd, VFD_GEAR_STOP);
    TEST_ASSERT_TRUE(pin_last_val(P_FWD.raw,  &v)); TEST_ASSERT_FALSE(v);
    TEST_ASSERT_TRUE(pin_last_val(P_SPD1.raw, &v)); TEST_ASSERT_FALSE(v);
    TEST_ASSERT_TRUE(pin_last_val(P_SPD2.raw, &v)); TEST_ASSERT_FALSE(v);
}

static void test_dir_switch_clears_all_pins(void)
{
    bool v;
    drv_vfd_run(&g_vfd, VFD_GEAR_FWD_1);
    pin_log_reset();
    drv_vfd_run(&g_vfd, VFD_GEAR_REV_1); /* 方向切换：立即关断所有 IO */
    TEST_ASSERT_TRUE(pin_last_val(P_FWD.raw,  &v)); TEST_ASSERT_FALSE(v);
    TEST_ASSERT_TRUE(pin_last_val(P_REV.raw,  &v)); TEST_ASSERT_FALSE(v);
    TEST_ASSERT_TRUE(pin_last_val(P_SPD1.raw, &v)); TEST_ASSERT_FALSE(v);
}

static void test_fault_reset_sets_rst_pin(void)
{
    bool v;
    drv_vfd_fault_reset(&g_vfd);
    TEST_ASSERT_TRUE(pin_last_val(P_RST.raw, &v)); TEST_ASSERT_TRUE(v);
    TEST_ASSERT_TRUE(pin_last_val(P_FWD.raw, &v)); TEST_ASSERT_FALSE(v);
}

/* =========================================================================
 * D. Modbus 操作与通信事件
 * ========================================================================= */

static void test_set_freq_writes_correct_register(void)
{
    TEST_ASSERT_EQUAL_INT(SW_OK, drv_vfd_set_freq(&g_vfd, 50U));
    TEST_ASSERT_EQUAL_INT(0x2001, modbus_stub_last_write_reg());
    TEST_ASSERT_EQUAL_UINT16(50U, modbus_stub_last_write_val());
}

static void test_set_freq_comm_fail_returns_err(void)
{
    modbus_stub_set_write_result(-1);
    TEST_ASSERT_EQUAL_INT(SW_ERR_COMM, drv_vfd_set_freq(&g_vfd, 50U));
}

static void test_comm_lost_event_after_failures(void)
{
    g_vfd.mb_connected = false;
    modbus_stub_set_connect_result(-1);
    drv_vfd_set_freq(&g_vfd, 10U);
    drv_vfd_set_freq(&g_vfd, 10U);
    drv_vfd_set_freq(&g_vfd, 10U);
    TEST_ASSERT_EQUAL_INT(HAL_VFD_EVT_COMM_LOST, s_last_event);
}

static void test_comm_restored_event(void)
{
    g_vfd.comm_ok      = false;
    g_vfd.mb_connected = true;
    modbus_stub_set_write_result(1);
    drv_vfd_set_freq(&g_vfd, 10U);
    TEST_ASSERT_EQUAL_INT(HAL_VFD_EVT_COMM_RESTORED, s_last_event);
}

static void test_get_fault_code_reads_and_caches(void)
{
    uint16_t code = 0U;
    modbus_stub_set_read_result(1, 0x0005U);
    TEST_ASSERT_EQUAL_INT(SW_OK, drv_vfd_get_fault_code(&g_vfd, &code));
    TEST_ASSERT_EQUAL_UINT16(0x0005U, code);
    TEST_ASSERT_EQUAL_UINT16(0x0005U, drv_vfd_get_cached_fault_code(&g_vfd));
    TEST_ASSERT_TRUE(g_vfd.fault_active);
}

static void test_get_fault_code_null_out(void)
{
    TEST_ASSERT_EQUAL_INT(SW_ERR_PARAM, drv_vfd_get_fault_code(&g_vfd, NULL));
}

/* =========================================================================
 * E. 初始化异常
 * ========================================================================= */

/* modbus_new_rtu 返回 NULL（上下文创建失败） → init 应返回 SW_ERR_HW */
static void test_init_modbus_ctx_fail(void)
{
    drv_vfd_t tmp;
    modbus_stub_set_new_rtu_fail(true);
    sw_err_t r = drv_vfd_init(&tmp, "/dev/ttyS0", 9600, 2,
                               P_FWD, P_REV, P_RST, fake_do_set);
    TEST_ASSERT_EQUAL_INT(SW_ERR_HW, r);
    /* 失败后 serial_port 应被清零，实例不可用 */
    TEST_ASSERT_NULL(tmp.serial_port);
}

/* =========================================================================
 * F. 运行时边界与意外情况
 * ========================================================================= */

/* 对已停止的 VFD 再次发送 STOP，不应崩溃也不应改变状态 */
static void test_run_stop_when_already_stopped(void)
{
    TEST_ASSERT_EQUAL_INT(SW_OK, drv_vfd_run(&g_vfd, VFD_GEAR_STOP));
    TEST_ASSERT_EQUAL_INT(HAL_VFD_STATE_STOPPED, drv_vfd_get_state(&g_vfd));
}

/* 方向切换等待中，发送 STOP 应立即取消待处理切换 */
static void test_dir_switch_stop_cancels_pending(void)
{
    bool v;
    drv_vfd_run(&g_vfd, VFD_GEAR_FWD_1);       /* 起步 */
    drv_vfd_run(&g_vfd, VFD_GEAR_REV_1);       /* 触发方向切换，pending=REV_1 */
    TEST_ASSERT_EQUAL(VFD_GEAR_REV_1, g_vfd.pending_gear);

    pin_log_reset();
    drv_vfd_run(&g_vfd, VFD_GEAR_STOP);         /* STOP 应立即取消 pending */

    TEST_ASSERT_EQUAL(VFD_GEAR_STOP, g_vfd.pending_gear);
    TEST_ASSERT_EQUAL(VFD_GEAR_STOP, g_vfd.gear);
    TEST_ASSERT_TRUE(pin_last_val(P_FWD.raw, &v));
    TEST_ASSERT_FALSE(v);
}

/* 方向切换等待中，发送相同方向的新挡位 → 更新 pending_gear，不重置计时器 */
static void test_dir_switch_same_dir_updates_pending(void)
{
    drv_vfd_run(&g_vfd, VFD_GEAR_FWD_1);
    drv_vfd_run(&g_vfd, VFD_GEAR_REV_1);        /* pending=REV_1 */
    TEST_ASSERT_EQUAL_INT(SW_OK, drv_vfd_run(&g_vfd, VFD_GEAR_REV_2));  /* 同向，改挡 */
    TEST_ASSERT_EQUAL(VFD_GEAR_REV_2, g_vfd.pending_gear);
    TEST_ASSERT_EQUAL(VFD_GEAR_STOP, g_vfd.gear); /* 仍在等待，没有立即执行 */
}

/* 方向切换等待中，发送反向（等同于取消），立即执行原方向挡位 */
static void test_dir_switch_opposite_dir_executes_immediately(void)
{
    bool v;
    drv_vfd_run(&g_vfd, VFD_GEAR_FWD_1);
    drv_vfd_run(&g_vfd, VFD_GEAR_REV_1);        /* pending=REV_1 */
    pin_log_reset();
    TEST_ASSERT_EQUAL_INT(SW_OK, drv_vfd_run(&g_vfd, VFD_GEAR_FWD_1)); /* 反向 pending → 立即执行 */

    TEST_ASSERT_EQUAL(VFD_GEAR_STOP, g_vfd.pending_gear); /* 不再等待 */
    TEST_ASSERT_EQUAL_INT(HAL_VFD_STATE_FWD, drv_vfd_get_state(&g_vfd));
    TEST_ASSERT_TRUE(pin_last_val(P_FWD.raw, &v));
    TEST_ASSERT_TRUE(v);
}

/* fault_reset 应取消待处理的方向切换 */
static void test_fault_reset_cancels_dir_switch_pending(void)
{
    drv_vfd_run(&g_vfd, VFD_GEAR_FWD_1);
    drv_vfd_run(&g_vfd, VFD_GEAR_REV_1);        /* pending=REV_1 */
    TEST_ASSERT_EQUAL(VFD_GEAR_REV_1, g_vfd.pending_gear);

    drv_vfd_fault_reset(&g_vfd);

    TEST_ASSERT_EQUAL(VFD_GEAR_STOP, g_vfd.pending_gear);
    TEST_ASSERT_EQUAL(VFD_GEAR_STOP, g_vfd.gear);
}

/* event_cb 设为 NULL 后触发通信失败，不应崩溃 */
static void test_null_event_cb_no_crash(void)
{
    drv_vfd_register_event_cb(&g_vfd, NULL);
    g_vfd.mb_connected = false;
    modbus_stub_set_connect_result(-1);
    /* 连续触发 COMM_LOST，但回调为 NULL，不应崩溃 */
    drv_vfd_set_freq(&g_vfd, 10U);
    drv_vfd_set_freq(&g_vfd, 10U);
    drv_vfd_set_freq(&g_vfd, 10U);
    /* 恢复回调，否则后续测试拿不到事件 */
    drv_vfd_register_event_cb(&g_vfd, event_cb);
}

/* =========================================================================
 * G. 未覆盖接口
 * ========================================================================= */

static void test_read_current_success(void)
{
    uint16_t cur = 0U;
    modbus_stub_set_read_result(1, 150U);
    TEST_ASSERT_EQUAL_INT(SW_OK, drv_vfd_read_current(&g_vfd, &cur));
    TEST_ASSERT_EQUAL_UINT16(150U, cur);
}

static void test_read_current_null_out(void)
{
    TEST_ASSERT_EQUAL_INT(SW_ERR_PARAM, drv_vfd_read_current(&g_vfd, NULL));
}

static void test_read_current_comm_fail(void)
{
    uint16_t cur = 0U;
    modbus_stub_set_read_result(-1, 0U);
    TEST_ASSERT_EQUAL_INT(SW_ERR_COMM, drv_vfd_read_current(&g_vfd, &cur));
}

static void test_read_status_success(void)
{
    uint16_t status = 0U;
    modbus_stub_set_read_result(1, 0x0003U);
    TEST_ASSERT_EQUAL_INT(SW_OK, drv_vfd_read_status(&g_vfd, &status));
    TEST_ASSERT_EQUAL_UINT16(0x0003U, status);
}

static void test_read_status_null_out(void)
{
    TEST_ASSERT_EQUAL_INT(SW_ERR_PARAM, drv_vfd_read_status(&g_vfd, NULL));
}

static void test_set_monitor_mask_uninit(void)
{
    drv_vfd_t tmp; memset(&tmp, 0, sizeof(tmp));
    TEST_ASSERT_EQUAL_INT(SW_ERR_NOT_INIT,
        drv_vfd_set_monitor_mask(&tmp, DRV_VFD_MON_NONE));
}

/* =========================================================================
 * main
 * ========================================================================= */
int main(void)
{
    UNITY_BEGIN();

    /* A */
    RUN_TEST(test_init_null_vfd);
    RUN_TEST(test_init_null_port);
    RUN_TEST(test_init_null_do_set);
    RUN_TEST(test_config_null_cfg);
    RUN_TEST(test_config_null_pin);
    RUN_TEST(test_config_zero_mapping);
    RUN_TEST(test_run_gear_too_large);
    RUN_TEST(test_run_gear_too_small);
    RUN_TEST(test_run_reverse_no_rev_pin);
    RUN_TEST(test_run_uninit_returns_not_init);
    RUN_TEST(test_set_freq_uninit_returns_not_init);
    RUN_TEST(test_fault_reset_uninit_returns_not_init);

    /* B */
    RUN_TEST(test_get_state_null);
    RUN_TEST(test_get_cached_fault_null);
    RUN_TEST(test_get_cached_current_null);
    RUN_TEST(test_get_state_fwd_after_run);
    RUN_TEST(test_get_state_rev_after_run);
    RUN_TEST(test_get_state_stop_after_run);

    /* C */
    RUN_TEST(test_run_fwd1_sets_correct_pins);
    RUN_TEST(test_run_rev1_sets_correct_pins);
    RUN_TEST(test_run_stop_clears_all_pins);
    RUN_TEST(test_dir_switch_clears_all_pins);
    RUN_TEST(test_fault_reset_sets_rst_pin);

    /* D */
    RUN_TEST(test_set_freq_writes_correct_register);
    RUN_TEST(test_set_freq_comm_fail_returns_err);
    RUN_TEST(test_comm_lost_event_after_failures);
    RUN_TEST(test_comm_restored_event);
    RUN_TEST(test_get_fault_code_reads_and_caches);
    RUN_TEST(test_get_fault_code_null_out);

    /* E */
    RUN_TEST(test_init_modbus_ctx_fail);

    /* F */
    RUN_TEST(test_run_stop_when_already_stopped);
    RUN_TEST(test_dir_switch_stop_cancels_pending);
    RUN_TEST(test_dir_switch_same_dir_updates_pending);
    RUN_TEST(test_dir_switch_opposite_dir_executes_immediately);
    RUN_TEST(test_fault_reset_cancels_dir_switch_pending);
    RUN_TEST(test_null_event_cb_no_crash);

    /* G */
    RUN_TEST(test_read_current_success);
    RUN_TEST(test_read_current_null_out);
    RUN_TEST(test_read_current_comm_fail);
    RUN_TEST(test_read_status_success);
    RUN_TEST(test_read_status_null_out);
    RUN_TEST(test_set_monitor_mask_uninit);

    return UNITY_END();
}
