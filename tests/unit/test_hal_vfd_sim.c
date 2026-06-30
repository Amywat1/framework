/**
 * @file    test_hal_vfd_sim.c
 * @brief   hal_vfd_sim 变频器仿真 HAL 单元测试
 *
 * 分组：
 *   A. 初始化
 *   B. 运行 / 停止 / 故障复位
 *   C. 读取接口
 *   D. 参数校验
 */

#include "ports/hal/hal_vfd_port.h"
#include "common/vfd_types.h"
#include "common/sw_error.h"
#include "unity.h"

#include <stdbool.h>
#include <stdint.h>

/* hal_vfd_sim.c 中对应的 VFD 实例 ID（与 m8_vfd_table.h 一致）*/
#define TEST_VFD_GANTRY  0
#define TEST_VFD_BRUSH   1
#define TEST_VFD_INVALID 99

/* 由链接的 hal_vfd_sim.c 提供 */
void hal_vfd_sim_register(void);

void setUp(void)
{
    hal_vfd_sim_register();
    hal_vfd_get_ops()->init();
}

void tearDown(void) {}

/* =========================================================================
 * A. 初始化
 * ========================================================================= */

static void test_init_gantry_stopped(void)
{
    TEST_ASSERT_EQUAL_INT(HAL_VFD_STATE_STOPPED,
        hal_vfd_get_ops()->get_state(TEST_VFD_GANTRY));
}

static void test_init_brush_stopped(void)
{
    TEST_ASSERT_EQUAL_INT(HAL_VFD_STATE_STOPPED,
        hal_vfd_get_ops()->get_state(TEST_VFD_BRUSH));
}

/* =========================================================================
 * B. 运行 / 停止 / 故障复位
 * ========================================================================= */

static void test_run_fwd_gantry(void)
{
    TEST_ASSERT_EQUAL_INT(SW_OK,
        hal_vfd_get_ops()->run(TEST_VFD_GANTRY, (hal_vfd_gear_t)1));
    TEST_ASSERT_EQUAL_INT(HAL_VFD_STATE_FWD,
        hal_vfd_get_ops()->get_state(TEST_VFD_GANTRY));
}

static void test_run_rev_gantry(void)
{
    TEST_ASSERT_EQUAL_INT(SW_OK,
        hal_vfd_get_ops()->run(TEST_VFD_GANTRY, (hal_vfd_gear_t)-1));
    TEST_ASSERT_EQUAL_INT(HAL_VFD_STATE_REV,
        hal_vfd_get_ops()->get_state(TEST_VFD_GANTRY));
}

static void test_run_gear_zero_stops(void)
{
    hal_vfd_get_ops()->run(TEST_VFD_GANTRY, (hal_vfd_gear_t)1);
    TEST_ASSERT_EQUAL_INT(SW_OK,
        hal_vfd_get_ops()->run(TEST_VFD_GANTRY, (hal_vfd_gear_t)0));
    TEST_ASSERT_EQUAL_INT(HAL_VFD_STATE_STOPPED,
        hal_vfd_get_ops()->get_state(TEST_VFD_GANTRY));
}

static void test_stop_returns_stopped(void)
{
    hal_vfd_get_ops()->run(TEST_VFD_BRUSH, (hal_vfd_gear_t)1);
    TEST_ASSERT_EQUAL_INT(SW_OK,
        hal_vfd_get_ops()->stop(TEST_VFD_BRUSH));
    TEST_ASSERT_EQUAL_INT(HAL_VFD_STATE_STOPPED,
        hal_vfd_get_ops()->get_state(TEST_VFD_BRUSH));
}

static void test_fault_reset_returns_stopped(void)
{
    hal_vfd_get_ops()->run(TEST_VFD_GANTRY, (hal_vfd_gear_t)1);
    TEST_ASSERT_EQUAL_INT(SW_OK,
        hal_vfd_get_ops()->fault_reset(TEST_VFD_GANTRY));
    TEST_ASSERT_EQUAL_INT(HAL_VFD_STATE_STOPPED,
        hal_vfd_get_ops()->get_state(TEST_VFD_GANTRY));
}

static void test_set_freq_returns_ok(void)
{
    TEST_ASSERT_EQUAL_INT(SW_OK,
        hal_vfd_get_ops()->set_freq(TEST_VFD_GANTRY, 5000U));
}

/* =========================================================================
 * C. 读取接口
 * ========================================================================= */

static void test_read_returns_ok_and_zero(void)
{
    uint16_t val = 0xFFU;
    TEST_ASSERT_EQUAL_INT(SW_OK,
        hal_vfd_get_ops()->read(TEST_VFD_GANTRY, HAL_VFD_REG_STATE, &val));
    TEST_ASSERT_EQUAL_UINT16(0U, val);
}

static void test_get_cached_returns_ok_and_zero(void)
{
    uint16_t val = 0xFFU;
    TEST_ASSERT_EQUAL_INT(SW_OK,
        hal_vfd_get_ops()->get_cached(TEST_VFD_BRUSH, HAL_VFD_REG_CURRENT, &val));
    TEST_ASSERT_EQUAL_UINT16(0U, val);
}

static void test_read_null_ptr_returns_err(void)
{
    TEST_ASSERT_EQUAL_INT(SW_ERR_PARAM,
        hal_vfd_get_ops()->read(TEST_VFD_GANTRY, HAL_VFD_REG_STATE, NULL));
}

/* =========================================================================
 * D. 参数校验（无效 ID）
 * ========================================================================= */

static void test_run_invalid_id_returns_err(void)
{
    TEST_ASSERT_EQUAL_INT(SW_ERR_PARAM,
        hal_vfd_get_ops()->run(TEST_VFD_INVALID, (hal_vfd_gear_t)1));
}

static void test_brush_rev_invalid(void)
{
    /* 刷子不支持反转（仅 GANTRY 支持负档位）*/
    TEST_ASSERT_EQUAL_INT(SW_ERR_PARAM,
        hal_vfd_get_ops()->run(TEST_VFD_BRUSH, (hal_vfd_gear_t)-1));
}

static void test_stop_invalid_id_returns_err(void)
{
    TEST_ASSERT_EQUAL_INT(SW_ERR_PARAM,
        hal_vfd_get_ops()->stop(TEST_VFD_INVALID));
}

static void test_fault_reset_invalid_id_returns_err(void)
{
    TEST_ASSERT_EQUAL_INT(SW_ERR_PARAM,
        hal_vfd_get_ops()->fault_reset(TEST_VFD_INVALID));
}

/* =========================================================================
 * E. 补充场景
 * ========================================================================= */

static void test_get_state_invalid_id_returns_stopped(void)
{
    /* 无效 ID 时 get_state 返回安全默认值 STOPPED */
    TEST_ASSERT_EQUAL_INT(HAL_VFD_STATE_STOPPED,
        hal_vfd_get_ops()->get_state(TEST_VFD_INVALID));
}

static void test_brush_run_fwd(void)
{
    TEST_ASSERT_EQUAL_INT(SW_OK,
        hal_vfd_get_ops()->run(TEST_VFD_BRUSH, (hal_vfd_gear_t)1));
    TEST_ASSERT_EQUAL_INT(HAL_VFD_STATE_FWD,
        hal_vfd_get_ops()->get_state(TEST_VFD_BRUSH));
}

static void test_set_freq_invalid_id_returns_err(void)
{
    TEST_ASSERT_EQUAL_INT(SW_ERR_PARAM,
        hal_vfd_get_ops()->set_freq(TEST_VFD_INVALID, 5000U));
}

static void test_get_cached_null_ptr_returns_err(void)
{
    TEST_ASSERT_EQUAL_INT(SW_ERR_PARAM,
        hal_vfd_get_ops()->get_cached(TEST_VFD_GANTRY, HAL_VFD_REG_CURRENT, NULL));
}

static void test_register_event_cb_does_not_crash(void)
{
    /* 仿真回调注册为空操作，不应崩溃 */
    hal_vfd_get_ops()->register_event_cb(TEST_VFD_GANTRY, NULL);
    /* 无异常即通过 */
}

static void test_state_unchanged_after_set_freq(void)
{
    hal_vfd_get_ops()->run(TEST_VFD_GANTRY, (hal_vfd_gear_t)1);
    TEST_ASSERT_EQUAL_INT(HAL_VFD_STATE_FWD,
        hal_vfd_get_ops()->get_state(TEST_VFD_GANTRY));

    hal_vfd_get_ops()->set_freq(TEST_VFD_GANTRY, 3000U);
    /* set_freq 不应改变运行状态 */
    TEST_ASSERT_EQUAL_INT(HAL_VFD_STATE_FWD,
        hal_vfd_get_ops()->get_state(TEST_VFD_GANTRY));
}

int main(void)
{
    UNITY_BEGIN();

    RUN_TEST(test_init_gantry_stopped);
    RUN_TEST(test_init_brush_stopped);

    RUN_TEST(test_run_fwd_gantry);
    RUN_TEST(test_run_rev_gantry);
    RUN_TEST(test_run_gear_zero_stops);
    RUN_TEST(test_stop_returns_stopped);
    RUN_TEST(test_fault_reset_returns_stopped);
    RUN_TEST(test_set_freq_returns_ok);

    RUN_TEST(test_read_returns_ok_and_zero);
    RUN_TEST(test_get_cached_returns_ok_and_zero);
    RUN_TEST(test_read_null_ptr_returns_err);

    RUN_TEST(test_run_invalid_id_returns_err);
    RUN_TEST(test_brush_rev_invalid);
    RUN_TEST(test_stop_invalid_id_returns_err);
    RUN_TEST(test_fault_reset_invalid_id_returns_err);

    RUN_TEST(test_get_state_invalid_id_returns_stopped);
    RUN_TEST(test_brush_run_fwd);
    RUN_TEST(test_set_freq_invalid_id_returns_err);
    RUN_TEST(test_get_cached_null_ptr_returns_err);
    RUN_TEST(test_register_event_cb_does_not_crash);
    RUN_TEST(test_state_unchanged_after_set_freq);

    return UNITY_END();
}
