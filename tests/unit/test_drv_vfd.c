/**
 * @file    test_drv_vfd.c
 * @brief   drv_vfd 单元测试
 *
 * 分组：
 *   A. 参数校验
 *   B. 纯逻辑 / get_state / get_cached
 *   C. IO 引脚控制
 *   D. read / write / get_cached 统一接口
 *   E. 通信事件
 *   F. 初始化异常
 *   G. 运行时边界与意外情况
 *   H. fault_reset 行为（IO 路径 + Modbus 路径）
 *
 * 设计约束：
 *   drv_vfd_init 的全局表无法跨测试重置，全文件只初始化一次 g_vfd。
 *   setUp 将 g_vfd 归零至停止态，设置 DRV_VFD_MON_NONE 防后台干扰 stub 计数。
 *
 * 当前厂商（士林 VFD）关键寄存器：
 *   STATE=0x1001, FAULT_CODE=0x1007, CURRENT=0x1004
 *   CLEAR_FAULT=0x1000, DATA=0x1101
 *   VFD_REG_FREQ_SET 未定义 → DRV_VFD_REG_FREQ 写返回 SW_ERR_PARAM
 */

#include "adapters/hal/linux_hw/drv/drv_vfd.h"
#include "tests/support/modbus_stub.h"
#include "unity.h"

#include <string.h>

/* -------------------------------------------------------------------------
 * 引脚句柄
 * ------------------------------------------------------------------------- */
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
    g_vfd.pin_rev         = P_REV;
    g_vfd.pin_rst         = P_RST;
    g_vfd.fault_active    = false;
    g_vfd.cached_fault_code = 0U;
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

static void test_write_uninit_returns_not_init(void)
{
    drv_vfd_t tmp; memset(&tmp, 0, sizeof(tmp));
    TEST_ASSERT_EQUAL_INT(SW_ERR_NOT_INIT,
        drv_vfd_write(&tmp, DRV_VFD_REG_CLEAR_FAULT, 0U));
}

static void test_read_uninit_returns_not_init(void)
{
    drv_vfd_t tmp; memset(&tmp, 0, sizeof(tmp));
    uint16_t val;
    TEST_ASSERT_EQUAL_INT(SW_ERR_NOT_INIT,
        drv_vfd_read(&tmp, DRV_VFD_REG_CURRENT, &val));
}

static void test_fault_reset_uninit_returns_not_init(void)
{
    drv_vfd_t tmp; memset(&tmp, 0, sizeof(tmp));
    TEST_ASSERT_EQUAL_INT(SW_ERR_NOT_INIT, drv_vfd_fault_reset(&tmp));
}

/* =========================================================================
 * B. 纯逻辑 / get_state / get_cached
 * ========================================================================= */

static void test_get_state_null(void)
{
    TEST_ASSERT_EQUAL_INT(HAL_VFD_STATE_STOPPED, drv_vfd_get_state(NULL));
}

static void test_get_cached_null_vfd(void)
{
    /* vfd=NULL 时所有 reg 均返回 0 */
    TEST_ASSERT_EQUAL_UINT16(0U, drv_vfd_get_cached(NULL, DRV_VFD_REG_FAULT_CODE));
    TEST_ASSERT_EQUAL_UINT16(0U, drv_vfd_get_cached(NULL, DRV_VFD_REG_CURRENT));
}

static void test_get_cached_unsupported_reg_returns_zero(void)
{
    /* STATE / FREQ / CLEAR_FAULT 不缓存，返回 0 */
    TEST_ASSERT_EQUAL_UINT16(0U, drv_vfd_get_cached(&g_vfd, DRV_VFD_REG_STATE));
    TEST_ASSERT_EQUAL_UINT16(0U, drv_vfd_get_cached(&g_vfd, DRV_VFD_REG_FREQ));
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
    TEST_ASSERT_TRUE(pin_last_val(P_SPD1.raw, &v)); TEST_ASSERT_TRUE(v);
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
    drv_vfd_run(&g_vfd, VFD_GEAR_REV_1);
    TEST_ASSERT_TRUE(pin_last_val(P_FWD.raw,  &v)); TEST_ASSERT_FALSE(v);
    TEST_ASSERT_TRUE(pin_last_val(P_REV.raw,  &v)); TEST_ASSERT_FALSE(v);
    TEST_ASSERT_TRUE(pin_last_val(P_SPD1.raw, &v)); TEST_ASSERT_FALSE(v);
}

/* =========================================================================
 * D. read / write / get_cached 统一接口
 * ========================================================================= */

/* --- drv_vfd_read --- */

static void test_read_fault_code_success_and_caches(void)
{
    uint16_t val = 0U;
    modbus_stub_set_read_result(1, 0x0005U);
    TEST_ASSERT_EQUAL_INT(SW_OK, drv_vfd_read(&g_vfd, DRV_VFD_REG_FAULT_CODE, &val));
    TEST_ASSERT_EQUAL_UINT16(0x0005U, val);
    TEST_ASSERT_EQUAL_UINT16(0x0005U, drv_vfd_get_cached(&g_vfd, DRV_VFD_REG_FAULT_CODE));
    TEST_ASSERT_TRUE(g_vfd.fault_active);
}

static void test_read_fault_code_null_out(void)
{
    TEST_ASSERT_EQUAL_INT(SW_ERR_PARAM,
        drv_vfd_read(&g_vfd, DRV_VFD_REG_FAULT_CODE, NULL));
}

/* 故障码从非零变为 0 时，fault_active 清零并触发 FAULT_CLEARED 事件 */
static void test_read_fault_code_clears_fault_active(void)
{
    g_vfd.fault_active      = true;
    g_vfd.cached_fault_code = 5U;
    uint16_t val = 0xFFFFU;
    modbus_stub_set_read_result(1, 0U);  /* 无故障 */
    drv_vfd_read(&g_vfd, DRV_VFD_REG_FAULT_CODE, &val);
    TEST_ASSERT_EQUAL_UINT16(0U, val);
    TEST_ASSERT_FALSE(g_vfd.fault_active);
    TEST_ASSERT_EQUAL_INT(HAL_VFD_EVT_FAULT_CLEARED, s_last_event);
}

/* 故障码从 0 变为非零时，fault_active 置位并触发 FAULT_DETECTED 事件 */
static void test_read_fault_code_sets_fault_active(void)
{
    modbus_stub_set_read_result(1, 0x0010U);
    uint16_t val = 0U;
    drv_vfd_read(&g_vfd, DRV_VFD_REG_FAULT_CODE, &val);
    TEST_ASSERT_TRUE(g_vfd.fault_active);
    TEST_ASSERT_EQUAL_INT(HAL_VFD_EVT_FAULT_DETECTED, s_last_event);
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

/* 读接口不支持只写寄存器（FREQ / CLEAR_FAULT） */
static void test_read_writeonly_reg_returns_param(void)
{
    uint16_t val;
    TEST_ASSERT_EQUAL_INT(SW_ERR_PARAM, drv_vfd_read(&g_vfd, DRV_VFD_REG_FREQ, &val));
    TEST_ASSERT_EQUAL_INT(SW_ERR_PARAM, drv_vfd_read(&g_vfd, DRV_VFD_REG_CLEAR_FAULT, &val));
}

/* --- drv_vfd_write --- */

/* 士林 VFD 未定义 VFD_REG_FREQ_SET，写频率返回参数错误 */
static void test_write_freq_not_supported_for_vendor(void)
{
    TEST_ASSERT_EQUAL_INT(SW_ERR_PARAM,
        drv_vfd_write(&g_vfd, DRV_VFD_REG_FREQ, 50U));
}

/* 写接口不支持只读寄存器 */
static void test_write_readonly_reg_returns_param(void)
{
    TEST_ASSERT_EQUAL_INT(SW_ERR_PARAM, drv_vfd_write(&g_vfd, DRV_VFD_REG_STATE, 0U));
    TEST_ASSERT_EQUAL_INT(SW_ERR_PARAM, drv_vfd_write(&g_vfd, DRV_VFD_REG_FAULT_CODE, 0U));
    TEST_ASSERT_EQUAL_INT(SW_ERR_PARAM, drv_vfd_write(&g_vfd, DRV_VFD_REG_CURRENT, 0U));
}

/* 士林 CLEAR_FAULT：写寄存器 0x1000，数据固定为 0x1101（忽略 val 参数）*/
static void test_write_clear_fault_correct_reg_and_data(void)
{
    TEST_ASSERT_EQUAL_INT(SW_OK, drv_vfd_write(&g_vfd, DRV_VFD_REG_CLEAR_FAULT, 0U));
    TEST_ASSERT_EQUAL_INT(0x1000,   modbus_stub_last_write_reg());
    TEST_ASSERT_EQUAL_UINT16(0x1101U, modbus_stub_last_write_val());
}

static void test_write_clear_fault_comm_fail(void)
{
    modbus_stub_set_write_result(-1);
    TEST_ASSERT_EQUAL_INT(SW_ERR_COMM,
        drv_vfd_write(&g_vfd, DRV_VFD_REG_CLEAR_FAULT, 0U));
}

/* --- drv_vfd_get_cached --- */

static void test_get_cached_fault_code(void)
{
    g_vfd.cached_fault_code = 0x0007U;
    TEST_ASSERT_EQUAL_UINT16(0x0007U,
        drv_vfd_get_cached(&g_vfd, DRV_VFD_REG_FAULT_CODE));
}

static void test_get_cached_current(void)
{
    g_vfd.cached_current = 200U;
    TEST_ASSERT_EQUAL_UINT16(200U,
        drv_vfd_get_cached(&g_vfd, DRV_VFD_REG_CURRENT));
}

/* =========================================================================
 * E. 通信事件
 * ========================================================================= */

/* 连续失败 VFD_COMM_FAIL_NOTIFY(=3) 次后触发 COMM_LOST */
static void test_comm_lost_event_after_failures(void)
{
    uint16_t dummy;
    g_vfd.mb_connected = false;
    modbus_stub_set_connect_result(-1);
    drv_vfd_read(&g_vfd, DRV_VFD_REG_CURRENT, &dummy);
    drv_vfd_read(&g_vfd, DRV_VFD_REG_CURRENT, &dummy);
    drv_vfd_read(&g_vfd, DRV_VFD_REG_CURRENT, &dummy);
    TEST_ASSERT_EQUAL_INT(HAL_VFD_EVT_COMM_LOST, s_last_event);
}

/* 通信从失效态恢复后触发 COMM_RESTORED */
static void test_comm_restored_event(void)
{
    uint16_t dummy;
    g_vfd.comm_ok      = false;
    g_vfd.mb_connected = true;
    modbus_stub_set_read_result(1, 0U);
    drv_vfd_read(&g_vfd, DRV_VFD_REG_CURRENT, &dummy);
    TEST_ASSERT_EQUAL_INT(HAL_VFD_EVT_COMM_RESTORED, s_last_event);
}

/* event_cb 设为 NULL 后触发通信失败，不应崩溃 */
static void test_null_event_cb_no_crash(void)
{
    uint16_t dummy;
    drv_vfd_register_event_cb(&g_vfd, NULL);
    g_vfd.mb_connected = false;
    modbus_stub_set_connect_result(-1);
    drv_vfd_read(&g_vfd, DRV_VFD_REG_CURRENT, &dummy);
    drv_vfd_read(&g_vfd, DRV_VFD_REG_CURRENT, &dummy);
    drv_vfd_read(&g_vfd, DRV_VFD_REG_CURRENT, &dummy);
    drv_vfd_register_event_cb(&g_vfd, event_cb);
}

/* =========================================================================
 * F. 初始化异常
 * ========================================================================= */

static void test_init_modbus_ctx_fail(void)
{
    drv_vfd_t tmp;
    modbus_stub_set_new_rtu_fail(true);
    TEST_ASSERT_EQUAL_INT(SW_ERR_HW,
        drv_vfd_init(&tmp, "/dev/ttyS0", 9600, 2, P_FWD, P_REV, P_RST, fake_do_set));
    TEST_ASSERT_NULL(tmp.serial_port);
}

/* =========================================================================
 * G. 运行时边界与意外情况
 * ========================================================================= */

static void test_run_stop_when_already_stopped(void)
{
    TEST_ASSERT_EQUAL_INT(SW_OK, drv_vfd_run(&g_vfd, VFD_GEAR_STOP));
    TEST_ASSERT_EQUAL_INT(HAL_VFD_STATE_STOPPED, drv_vfd_get_state(&g_vfd));
}

static void test_dir_switch_stop_cancels_pending(void)
{
    bool v;
    drv_vfd_run(&g_vfd, VFD_GEAR_FWD_1);
    drv_vfd_run(&g_vfd, VFD_GEAR_REV_1);
    TEST_ASSERT_EQUAL(VFD_GEAR_REV_1, g_vfd.pending_gear);
    pin_log_reset();
    drv_vfd_run(&g_vfd, VFD_GEAR_STOP);
    TEST_ASSERT_EQUAL(VFD_GEAR_STOP, g_vfd.pending_gear);
    TEST_ASSERT_EQUAL(VFD_GEAR_STOP, g_vfd.gear);
    TEST_ASSERT_TRUE(pin_last_val(P_FWD.raw, &v)); TEST_ASSERT_FALSE(v);
}

/* 等待中再次发送反转（同方向）→ 更新 pending，300ms 不重置 */
static void test_dir_switch_same_dir_updates_pending(void)
{
    drv_vfd_run(&g_vfd, VFD_GEAR_FWD_1);
    drv_vfd_run(&g_vfd, VFD_GEAR_REV_1);
    TEST_ASSERT_EQUAL_INT(SW_OK, drv_vfd_run(&g_vfd, VFD_GEAR_REV_2));
    TEST_ASSERT_EQUAL(VFD_GEAR_REV_2, g_vfd.pending_gear);
    TEST_ASSERT_EQUAL(VFD_GEAR_STOP, g_vfd.gear);
}

/* 等待中发送正转（原方向）→ 取消等待，立即执行 */
static void test_dir_switch_opposite_dir_executes_immediately(void)
{
    bool v;
    drv_vfd_run(&g_vfd, VFD_GEAR_FWD_1);
    drv_vfd_run(&g_vfd, VFD_GEAR_REV_1);
    pin_log_reset();
    drv_vfd_run(&g_vfd, VFD_GEAR_FWD_1);
    TEST_ASSERT_EQUAL(VFD_GEAR_STOP, g_vfd.pending_gear);
    TEST_ASSERT_EQUAL_INT(HAL_VFD_STATE_FWD, drv_vfd_get_state(&g_vfd));
    TEST_ASSERT_TRUE(pin_last_val(P_FWD.raw, &v)); TEST_ASSERT_TRUE(v);
}

static void test_fault_reset_cancels_dir_switch_pending(void)
{
    drv_vfd_run(&g_vfd, VFD_GEAR_FWD_1);
    drv_vfd_run(&g_vfd, VFD_GEAR_REV_1);
    TEST_ASSERT_EQUAL(VFD_GEAR_REV_1, g_vfd.pending_gear);
    drv_vfd_fault_reset(&g_vfd);
    TEST_ASSERT_EQUAL(VFD_GEAR_STOP, g_vfd.pending_gear);
    TEST_ASSERT_EQUAL(VFD_GEAR_STOP, g_vfd.gear);
}

/* =========================================================================
 * H. fault_reset 行为
 * ========================================================================= */

/* pin_rst 有效时：拉高 RST 引脚，先关断运行输出 */
static void test_fault_reset_io_path_sets_rst_pin(void)
{
    bool v;
    drv_vfd_fault_reset(&g_vfd);
    TEST_ASSERT_TRUE(pin_last_val(P_RST.raw, &v)); TEST_ASSERT_TRUE(v);
    TEST_ASSERT_TRUE(pin_last_val(P_FWD.raw, &v)); TEST_ASSERT_FALSE(v);
}

/* pin_rst 为 IO_HANDLE_NULL 时：走 Modbus 清故障路径（士林：写 0x1000=0x1101）*/
static void test_fault_reset_no_pin_uses_modbus(void)
{
    g_vfd.pin_rst = (io_do_t){IO_HANDLE_NULL};
    TEST_ASSERT_EQUAL_INT(SW_OK, drv_vfd_fault_reset(&g_vfd));
    /* RST 引脚未被操作（pin_rst 为 NULL，io_reset=false）*/
    TEST_ASSERT_FALSE(pin_last_val(P_RST.raw, NULL));
    /* Modbus 写了清故障寄存器 */
    TEST_ASSERT_EQUAL_INT(0x1000,    modbus_stub_last_write_reg());
    TEST_ASSERT_EQUAL_UINT16(0x1101U, modbus_stub_last_write_val());
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
    RUN_TEST(test_write_uninit_returns_not_init);
    RUN_TEST(test_read_uninit_returns_not_init);
    RUN_TEST(test_fault_reset_uninit_returns_not_init);

    /* B */
    RUN_TEST(test_get_state_null);
    RUN_TEST(test_get_cached_null_vfd);
    RUN_TEST(test_get_cached_unsupported_reg_returns_zero);
    RUN_TEST(test_get_state_fwd_after_run);
    RUN_TEST(test_get_state_rev_after_run);
    RUN_TEST(test_get_state_stop_after_run);

    /* C */
    RUN_TEST(test_run_fwd1_sets_correct_pins);
    RUN_TEST(test_run_rev1_sets_correct_pins);
    RUN_TEST(test_run_stop_clears_all_pins);
    RUN_TEST(test_dir_switch_clears_all_pins);

    /* D */
    RUN_TEST(test_read_fault_code_success_and_caches);
    RUN_TEST(test_read_fault_code_null_out);
    RUN_TEST(test_read_fault_code_clears_fault_active);
    RUN_TEST(test_read_fault_code_sets_fault_active);
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
    RUN_TEST(test_get_cached_fault_code);
    RUN_TEST(test_get_cached_current);

    /* E */
    RUN_TEST(test_comm_lost_event_after_failures);
    RUN_TEST(test_comm_restored_event);
    RUN_TEST(test_null_event_cb_no_crash);

    /* F */
    RUN_TEST(test_init_modbus_ctx_fail);

    /* G */
    RUN_TEST(test_run_stop_when_already_stopped);
    RUN_TEST(test_dir_switch_stop_cancels_pending);
    RUN_TEST(test_dir_switch_same_dir_updates_pending);
    RUN_TEST(test_dir_switch_opposite_dir_executes_immediately);
    RUN_TEST(test_fault_reset_cancels_dir_switch_pending);

    /* H */
    RUN_TEST(test_fault_reset_io_path_sets_rst_pin);
    RUN_TEST(test_fault_reset_no_pin_uses_modbus);

    return UNITY_END();
}
