/**
 * @file    test_hal_motor.c
 * @brief   hal_motor 通用电机 HAL 单元测试
 *
 * 分组：
 *   A. 绑定参数校验
 *   B. DO 方向输出
 *   C. 速度回调模式
 *   D. 限位检测
 *   E. 脉冲计数器
 *   F. 电流 / 状态 / 故障复位回调
 *   G. 无效 ID 与未绑定
 */

#include "adapters/hal/generic/hal_motor.h"
#include "ports/hal/hal_motor_port.h"
#include "ports/hal/hal_motor_bind.h"
#include "ports/hal/hal_io_port.h"
#include "common/io_handle.h"
#include "common/sw_error.h"
#include "unity.h"

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#define TEST_BOARD  1U

/* -------------------------------------------------------------------------
 * Mock IO 状态
 * pin 编号直接用作数组索引（1~3 有效）
 * ------------------------------------------------------------------------- */
static bool     s_do_state[4];   /* [1]=CW, [2]=CCW, [3]=STOP */
static bool     s_di_state[4];   /* [1]=cw_lim, [2]=ccw_lim, [3]=enc */
static int      s_pulse_val      = 100;
static sw_err_t s_pulse_clr_ret = SW_OK;

static sw_err_t mock_do_set(io_do_t pin, bool val)
{
    uint16_t p = io_handle_pin(io_do_raw(pin));
    if (p < 4U) { s_do_state[p] = val; }
    return SW_OK;
}

static bool mock_di_read(io_di_t pin)
{
    uint16_t p = io_handle_pin(io_di_raw(pin));
    return (p < 4U) ? s_di_state[p] : false;
}

static int mock_pulse_read(io_di_t pin)
{
    (void)pin;
    return s_pulse_val;
}

static sw_err_t mock_pulse_clear(io_di_t pin)
{
    (void)pin;
    return s_pulse_clr_ret;
}

static const hal_io_ops_t s_mock_io_ops = {
    .do_set      = mock_do_set,
    .di_read     = mock_di_read,
    .pulse_read  = mock_pulse_read,
    .pulse_clear = mock_pulse_clear,
};

/* -------------------------------------------------------------------------
 * Mock 回调
 * ------------------------------------------------------------------------- */
static bool s_cb_set_speed_called = false;
static int  s_cb_speed_ref        = 0;

static sw_err_t mock_set_speed(int speed_ref, void *ctx)
{
    (void)ctx;
    s_cb_set_speed_called = true;
    s_cb_speed_ref        = speed_ref;
    return SW_OK;
}

static sw_err_t mock_read_current(uint16_t *p, void *ctx)
{
    (void)ctx;
    *p = 1234U;
    return SW_OK;
}

static sw_err_t mock_read_status(uint16_t *p, void *ctx)
{
    (void)ctx;
    *p = 5678U;
    return SW_OK;
}

static bool s_fault_reset_called = false;

static sw_err_t mock_fault_reset(void *ctx)
{
    (void)ctx;
    s_fault_reset_called = true;
    return SW_OK;
}

/* -------------------------------------------------------------------------
 * 基础绑定配置（含编码器，无 VFD 回调）
 * ------------------------------------------------------------------------- */
static const hal_motor_bind_cfg_t k_base_cfg = {
    .io_cw        = IO_DO(TEST_BOARD, 1U),
    .io_ccw       = IO_DO(TEST_BOARD, 2U),
    .io_stop      = IO_DO(TEST_BOARD, 3U),
    .limit_io_cw  = IO_DI(TEST_BOARD, 1U),
    .limit_io_ccw = IO_DI(TEST_BOARD, 2U),
    .has_encoder  = true,
    .encoder_io   = IO_DI(TEST_BOARD, 3U),
};

void setUp(void)
{
    memset(s_do_state, 0, sizeof(s_do_state));
    memset(s_di_state, 0, sizeof(s_di_state));
    s_pulse_val          = 100;
    s_pulse_clr_ret      = SW_OK;
    s_cb_set_speed_called = false;
    s_cb_speed_ref        = 0;
    s_fault_reset_called  = false;

    hal_io_register(&s_mock_io_ops);
    hal_motor_generic_register();
    hal_motor_bind(0, &k_base_cfg);
}

void tearDown(void) {}

/* =========================================================================
 * A. 绑定参数校验
 * ========================================================================= */

static void test_bind_null_cfg(void)
{
    TEST_ASSERT_EQUAL_INT(SW_ERR_PARAM, hal_motor_bind(0, NULL));
}

static void test_bind_negative_id(void)
{
    TEST_ASSERT_EQUAL_INT(SW_ERR_PARAM, hal_motor_bind(-1, &k_base_cfg));
}

static void test_bind_id_out_of_range(void)
{
    TEST_ASSERT_EQUAL_INT(SW_ERR_PARAM,
        hal_motor_bind(HAL_MOTOR_BIND_SLOT_MAX, &k_base_cfg));
}

/* =========================================================================
 * B. DO 方向输出
 * ========================================================================= */

static void test_set_output_cw(void)
{
    TEST_ASSERT_EQUAL_INT(SW_OK, hal_motor_get_ops()->set_output(0, 100));
    TEST_ASSERT_TRUE(s_do_state[1]);   /* CW on */
    TEST_ASSERT_FALSE(s_do_state[2]);  /* CCW off */
}

static void test_set_output_ccw(void)
{
    TEST_ASSERT_EQUAL_INT(SW_OK, hal_motor_get_ops()->set_output(0, -100));
    TEST_ASSERT_TRUE(s_do_state[2]);   /* CCW on */
    TEST_ASSERT_FALSE(s_do_state[1]);  /* CW off */
}

static void test_set_output_stop(void)
{
    TEST_ASSERT_EQUAL_INT(SW_OK, hal_motor_get_ops()->set_output(0, 0));
    TEST_ASSERT_FALSE(s_do_state[1]); /* CW off */
    TEST_ASSERT_FALSE(s_do_state[2]); /* CCW off */
    TEST_ASSERT_TRUE(s_do_state[3]);  /* STOP on */
}

/* =========================================================================
 * C. 速度回调模式（VFD 注入）
 * ========================================================================= */

static void test_set_speed_callback_invoked(void)
{
    hal_motor_bind_cfg_t cfg = k_base_cfg;
    cfg.set_speed = mock_set_speed;
    hal_motor_bind(0, &cfg);

    TEST_ASSERT_EQUAL_INT(SW_OK, hal_motor_get_ops()->set_output(0, 2500));
    TEST_ASSERT_TRUE(s_cb_set_speed_called);
    TEST_ASSERT_EQUAL_INT(2500, s_cb_speed_ref);
}

static void test_set_speed_callback_stop(void)
{
    hal_motor_bind_cfg_t cfg = k_base_cfg;
    cfg.set_speed = mock_set_speed;
    hal_motor_bind(0, &cfg);

    TEST_ASSERT_EQUAL_INT(SW_OK, hal_motor_get_ops()->set_output(0, 0));
    TEST_ASSERT_EQUAL_INT(0, s_cb_speed_ref);
}

/* =========================================================================
 * D. 限位检测
 * ========================================================================= */

static void test_at_fwd_limit_true(void)
{
    s_di_state[1] = true; /* CW 限位 pin=1 */
    TEST_ASSERT_TRUE(hal_motor_get_ops()->at_fwd_limit(0));
}

static void test_at_fwd_limit_false(void)
{
    s_di_state[1] = false;
    TEST_ASSERT_FALSE(hal_motor_get_ops()->at_fwd_limit(0));
}

static void test_at_rev_limit_true(void)
{
    s_di_state[2] = true; /* CCW 限位 pin=2 */
    TEST_ASSERT_TRUE(hal_motor_get_ops()->at_rev_limit(0));
}

static void test_at_fwd_limit_no_di(void)
{
    /* 无效 DI 句柄 → 始终 false */
    hal_motor_bind_cfg_t cfg = k_base_cfg;
    cfg.limit_io_cw = (io_di_t){IO_HANDLE_NULL};
    hal_motor_bind(0, &cfg);

    s_di_state[1] = true;
    TEST_ASSERT_FALSE(hal_motor_get_ops()->at_fwd_limit(0));
}

/* =========================================================================
 * E. 脉冲计数器
 * ========================================================================= */

static void test_read_hw_pulse_ok(void)
{
    uint32_t val = 0;
    TEST_ASSERT_EQUAL_INT(SW_OK, hal_motor_get_ops()->read_hw_pulse(0, &val));
    TEST_ASSERT_EQUAL_UINT32(100U, val);
}

static void test_read_hw_pulse_comm_negative(void)
{
    uint32_t val = 0;
    s_pulse_val = -1;
    TEST_ASSERT_EQUAL_INT(SW_ERR_COMM,
        hal_motor_get_ops()->read_hw_pulse(0, &val));
}

static void test_read_hw_pulse_comm_sentinel(void)
{
    uint32_t val = 0;
    s_pulse_val = (int)0x0FFFFFFF;
    TEST_ASSERT_EQUAL_INT(SW_ERR_COMM,
        hal_motor_get_ops()->read_hw_pulse(0, &val));
}

static void test_read_hw_pulse_null_ptr(void)
{
    TEST_ASSERT_EQUAL_INT(SW_ERR_PARAM,
        hal_motor_get_ops()->read_hw_pulse(0, NULL));
}

static void test_read_hw_pulse_no_encoder(void)
{
    uint32_t val;
    hal_motor_bind_cfg_t cfg = k_base_cfg;
    cfg.has_encoder = false;
    hal_motor_bind(0, &cfg);
    TEST_ASSERT_EQUAL_INT(SW_ERR_PARAM,
        hal_motor_get_ops()->read_hw_pulse(0, &val));
}

static void test_clear_hw_pulse_ok(void)
{
    TEST_ASSERT_EQUAL_INT(SW_OK, hal_motor_get_ops()->clear_hw_pulse(0));
}

static void test_clear_hw_pulse_no_encoder(void)
{
    hal_motor_bind_cfg_t cfg = k_base_cfg;
    cfg.has_encoder = false;
    hal_motor_bind(0, &cfg);
    TEST_ASSERT_EQUAL_INT(SW_ERR_PARAM,
        hal_motor_get_ops()->clear_hw_pulse(0));
}

/* =========================================================================
 * F. 电流 / 状态 / 故障复位回调
 * ========================================================================= */

static void test_read_current_no_callback(void)
{
    uint16_t val;
    TEST_ASSERT_EQUAL_INT(SW_ERR_NOT_SUPPORT,
        hal_motor_get_ops()->read_current(0, &val));
}

static void test_read_current_with_callback(void)
{
    uint16_t val = 0;
    hal_motor_bind_cfg_t cfg = k_base_cfg;
    cfg.read_current = mock_read_current;
    hal_motor_bind(0, &cfg);

    TEST_ASSERT_EQUAL_INT(SW_OK, hal_motor_get_ops()->read_current(0, &val));
    TEST_ASSERT_EQUAL_UINT16(1234U, val);
}

static void test_read_status_no_callback(void)
{
    uint16_t val;
    TEST_ASSERT_EQUAL_INT(SW_ERR_NOT_SUPPORT,
        hal_motor_get_ops()->read_status(0, &val));
}

static void test_read_status_with_callback(void)
{
    uint16_t val = 0;
    hal_motor_bind_cfg_t cfg = k_base_cfg;
    cfg.read_status = mock_read_status;
    hal_motor_bind(0, &cfg);

    TEST_ASSERT_EQUAL_INT(SW_OK, hal_motor_get_ops()->read_status(0, &val));
    TEST_ASSERT_EQUAL_UINT16(5678U, val);
}

static void test_fault_reset_no_callback(void)
{
    TEST_ASSERT_EQUAL_INT(SW_ERR_NOT_SUPPORT,
        hal_motor_get_ops()->fault_reset(0));
}

static void test_fault_reset_with_callback(void)
{
    hal_motor_bind_cfg_t cfg = k_base_cfg;
    cfg.fault_reset = mock_fault_reset;
    hal_motor_bind(0, &cfg);

    TEST_ASSERT_EQUAL_INT(SW_OK, hal_motor_get_ops()->fault_reset(0));
    TEST_ASSERT_TRUE(s_fault_reset_called);
}

/* =========================================================================
 * G. 无效 ID 与未绑定
 * ========================================================================= */

static void test_set_output_invalid_id(void)
{
    /* HAL_MOTOR_BIND_SLOT_MAX 始终越界 */
    TEST_ASSERT_EQUAL_INT(SW_ERR_NOT_INIT,
        hal_motor_get_ops()->set_output(HAL_MOTOR_BIND_SLOT_MAX, 0));
}

static void test_at_fwd_limit_invalid_id(void)
{
    TEST_ASSERT_FALSE(hal_motor_get_ops()->at_fwd_limit(HAL_MOTOR_BIND_SLOT_MAX));
}

/* =========================================================================
 * H. 补充场景
 * ========================================================================= */

static void test_at_rev_limit_false(void)
{
    s_di_state[2] = false;
    TEST_ASSERT_FALSE(hal_motor_get_ops()->at_rev_limit(0));
}

static void test_read_current_null_ptr(void)
{
    hal_motor_bind_cfg_t cfg = k_base_cfg;
    cfg.read_current = mock_read_current;
    hal_motor_bind(0, &cfg);
    TEST_ASSERT_EQUAL_INT(SW_ERR_PARAM, hal_motor_get_ops()->read_current(0, NULL));
}

static void test_read_status_null_ptr(void)
{
    hal_motor_bind_cfg_t cfg = k_base_cfg;
    cfg.read_status = mock_read_status;
    hal_motor_bind(0, &cfg);
    TEST_ASSERT_EQUAL_INT(SW_ERR_PARAM, hal_motor_get_ops()->read_status(0, NULL));
}

/* IO 操作集不包含 do_set，set_output 应返回 SW_ERR_NOT_INIT */
static const hal_io_ops_t s_no_do_set_ops = {
    .di_read     = mock_di_read,
    .pulse_read  = mock_pulse_read,
    .pulse_clear = mock_pulse_clear,
};

static void test_set_output_no_io_do_set(void)
{
    hal_io_register(&s_no_do_set_ops);
    TEST_ASSERT_EQUAL_INT(SW_ERR_NOT_INIT,
        hal_motor_get_ops()->set_output(0, 100));
}

/* IO 操作集不包含 pulse_read / pulse_clear */
static const hal_io_ops_t s_no_pulse_ops = {
    .do_set  = mock_do_set,
    .di_read = mock_di_read,
};

static void test_read_hw_pulse_no_pulse_read_op(void)
{
    uint32_t val;
    hal_io_register(&s_no_pulse_ops);
    TEST_ASSERT_EQUAL_INT(SW_ERR_NOT_INIT,
        hal_motor_get_ops()->read_hw_pulse(0, &val));
}

static void test_clear_hw_pulse_no_pulse_clear_op(void)
{
    hal_io_register(&s_no_pulse_ops);
    TEST_ASSERT_EQUAL_INT(SW_ERR_NOT_INIT,
        hal_motor_get_ops()->clear_hw_pulse(0));
}

static sw_err_t mock_set_speed_fail(int speed_ref, void *ctx)
{
    (void)speed_ref; (void)ctx;
    return SW_ERR_COMM;
}

static void test_set_speed_callback_error_propagates(void)
{
    hal_motor_bind_cfg_t cfg = k_base_cfg;
    cfg.set_speed = mock_set_speed_fail;
    hal_motor_bind(0, &cfg);
    TEST_ASSERT_EQUAL_INT(SW_ERR_COMM,
        hal_motor_get_ops()->set_output(0, 1000));
}

static void test_set_output_partial_do_config(void)
{
    /* 只配置 CW 引脚，CCW / STOP 为 NULL：set_output 跳过无效引脚，返回 SW_OK */
    hal_motor_bind_cfg_t cfg = {
        .io_cw   = IO_DO(TEST_BOARD, 1U),
        .io_ccw  = (io_do_t){IO_HANDLE_NULL},
        .io_stop = (io_do_t){IO_HANDLE_NULL},
    };
    hal_motor_bind(0, &cfg);
    TEST_ASSERT_EQUAL_INT(SW_OK, hal_motor_get_ops()->set_output(0, 100));
    TEST_ASSERT_TRUE(s_do_state[1]);
}

int main(void)
{
    UNITY_BEGIN();

    RUN_TEST(test_bind_null_cfg);
    RUN_TEST(test_bind_negative_id);
    RUN_TEST(test_bind_id_out_of_range);

    RUN_TEST(test_set_output_cw);
    RUN_TEST(test_set_output_ccw);
    RUN_TEST(test_set_output_stop);

    RUN_TEST(test_set_speed_callback_invoked);
    RUN_TEST(test_set_speed_callback_stop);

    RUN_TEST(test_at_fwd_limit_true);
    RUN_TEST(test_at_fwd_limit_false);
    RUN_TEST(test_at_rev_limit_true);
    RUN_TEST(test_at_fwd_limit_no_di);

    RUN_TEST(test_read_hw_pulse_ok);
    RUN_TEST(test_read_hw_pulse_comm_negative);
    RUN_TEST(test_read_hw_pulse_comm_sentinel);
    RUN_TEST(test_read_hw_pulse_null_ptr);
    RUN_TEST(test_read_hw_pulse_no_encoder);
    RUN_TEST(test_clear_hw_pulse_ok);
    RUN_TEST(test_clear_hw_pulse_no_encoder);

    RUN_TEST(test_read_current_no_callback);
    RUN_TEST(test_read_current_with_callback);
    RUN_TEST(test_read_status_no_callback);
    RUN_TEST(test_read_status_with_callback);
    RUN_TEST(test_fault_reset_no_callback);
    RUN_TEST(test_fault_reset_with_callback);

    RUN_TEST(test_set_output_invalid_id);
    RUN_TEST(test_at_fwd_limit_invalid_id);

    RUN_TEST(test_at_rev_limit_false);
    RUN_TEST(test_read_current_null_ptr);
    RUN_TEST(test_read_status_null_ptr);
    RUN_TEST(test_set_output_no_io_do_set);
    RUN_TEST(test_read_hw_pulse_no_pulse_read_op);
    RUN_TEST(test_clear_hw_pulse_no_pulse_clear_op);
    RUN_TEST(test_set_speed_callback_error_propagates);
    RUN_TEST(test_set_output_partial_do_config);

    return UNITY_END();
}
