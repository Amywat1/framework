/**
 * @file    test_rear_lock.c
 * @brief   rear_lock 后轮锁止机构领域层单元测试
 *
 * 分组：
 *   B. 未初始化保护（须在 main() 中排在最前面，保证 rear_lock.c s_exec == NULL）
 *   A. 初始化参数校验
 *   C. 锁止 / 释放命令（FORWARD=推杆伸出锁止、REVERSE=推杆缩回释放）
 *   D. 停止与回原点
 *   E. motor_phase → rear_lock_state 映射
 *   F. 故障与恢复
 *
 * @note    setUp() 只重置 motor_executor_t，不调用 rear_lock_init()。
 *          各测试自行在函数体内调用 rear_lock_init(s_hexec, 0) 以进入已初始化状态。
 *          未初始化测试（B 组）须最先运行，确保 rear_lock.c 内 s_exec == NULL。
 */

#include "framework/domain/device_control/mechanism/rear_lock.h"
#include "motor/motor_executor.h"
#include "framework/common/sw_error.h"
#include "unity.h"

#include <string.h>
#include <stddef.h>

/* -------------------------------------------------------------------------
 * 最小 motor_executor_t 初始化辅助
 * ------------------------------------------------------------------------- */
static motor_executor_t  s_exec;
static motor_driver_t    s_drv;
static motor_driver_t   *s_drv_arr[1] = {&s_drv};
static motor_clock_t     s_clk;
static motor_sensors_t   s_sensors;
static motor_estop_t     s_estop;
static motor_ports_t     s_ports;
static motor_config_t    s_cfg;
static hal_motor_exec_t *const s_hexec = (hal_motor_exec_t *)&s_exec;

static void init_exec(void)
{
    memset(&s_cfg,     0, sizeof(s_cfg));
    memset(&s_drv,     0, sizeof(s_drv));
    memset(&s_clk,     0, sizeof(s_clk));
    memset(&s_sensors, 0, sizeof(s_sensors));
    memset(&s_estop,   0, sizeof(s_estop));
    memset(&s_ports,   0, sizeof(s_ports));

    s_cfg.motor_count                    = 1;
    s_cfg.driver_count                   = 1;
    s_cfg.watchdog_ms                    = 200;
    s_cfg.tick_ms                        = 20;
    s_cfg.motors[0].driver_index         = 0;
    s_cfg.motors[0].default_max_move_ms  = 10000;
    s_cfg.motors[0].gear_count           = 1;
    s_cfg.motors[0].gear_freq[0]         = 5000;

    s_ports.clock    = &s_clk;
    s_ports.drivers  = s_drv_arr;
    s_ports.encoders = NULL;
    s_ports.sensors  = &s_sensors;
    s_ports.estop    = &s_estop;

    motor_init(&s_exec, &s_cfg, &s_ports);
}

void setUp(void)
{
    init_exec();
    /* 不在此调用 rear_lock_init()，由各测试按需调用 */
}

void tearDown(void) {}

/* =========================================================================
 * B. 未初始化保护（列在 main() 最前面运行，此时 rear_lock.c s_exec == NULL）
 * ========================================================================= */

void test_rear_lock_not_init_lock(void)
{
    TEST_ASSERT_EQUAL(SW_ERR_NOT_INIT, rear_lock_lock(0, NULL));
}

void test_rear_lock_not_init_release(void)
{
    TEST_ASSERT_EQUAL(SW_ERR_NOT_INIT, rear_lock_release(0, NULL));
}

void test_rear_lock_not_init_stop(void)
{
    TEST_ASSERT_EQUAL(SW_ERR_NOT_INIT, rear_lock_stop());
}

void test_rear_lock_not_init_home(void)
{
    TEST_ASSERT_EQUAL(SW_ERR_NOT_INIT, rear_lock_home());
}

void test_rear_lock_not_init_recover(void)
{
    TEST_ASSERT_EQUAL(SW_ERR_NOT_INIT, rear_lock_recover(HAL_MOTOR_RECOVERY_MODULE_STOP));
}

void test_rear_lock_not_init_state_idle(void)
{
    /* 未初始化时 rear_lock_state() 应安全返回 IDLE */
    TEST_ASSERT_EQUAL(REAR_LOCK_STATE_IDLE, rear_lock_state());
}

void test_rear_lock_not_init_fault_code_none(void)
{
    TEST_ASSERT_EQUAL(MOTOR_FAULT_NONE, rear_lock_fault_code());
}

/* =========================================================================
 * A. 初始化参数校验
 * ========================================================================= */

void test_rear_lock_init_null_returns_err_param(void)
{
    TEST_ASSERT_EQUAL(SW_ERR_PARAM, rear_lock_init(NULL, 0));
}

void test_rear_lock_init_ok(void)
{
    TEST_ASSERT_EQUAL(SW_OK, rear_lock_init(s_hexec, 0));
}

void test_rear_lock_state_idle_after_init(void)
{
    rear_lock_init(s_hexec, 0);
    TEST_ASSERT_EQUAL(REAR_LOCK_STATE_IDLE, rear_lock_state());
}

void test_rear_lock_fault_code_none_after_init(void)
{
    rear_lock_init(s_hexec, 0);
    TEST_ASSERT_EQUAL(MOTOR_FAULT_NONE, rear_lock_fault_code());
}

/* =========================================================================
 * C. 锁止 / 释放命令（FORWARD = 锁止，REVERSE = 释放）
 * ========================================================================= */

void test_rear_lock_lock_returns_ok(void)
{
    rear_lock_init(s_hexec, 0);
    TEST_ASSERT_EQUAL(SW_OK, rear_lock_lock(0, NULL));
}

void test_rear_lock_lock_state_locking(void)
{
    rear_lock_init(s_hexec, 0);
    rear_lock_lock(0, NULL);
    /* 仿真桩 motor_run_continuous 立即置 RUNNING + FORWARD → LOCKING */
    TEST_ASSERT_EQUAL(REAR_LOCK_STATE_LOCKING, rear_lock_state());
}

void test_rear_lock_lock_direction_forward(void)
{
    rear_lock_init(s_hexec, 0);
    rear_lock_lock(0, NULL);
    TEST_ASSERT_EQUAL(MOTOR_DIR_FORWARD, motor_direction(&s_exec, 0));
}

void test_rear_lock_release_returns_ok(void)
{
    rear_lock_init(s_hexec, 0);
    TEST_ASSERT_EQUAL(SW_OK, rear_lock_release(0, NULL));
}

void test_rear_lock_release_state_releasing(void)
{
    rear_lock_init(s_hexec, 0);
    rear_lock_release(0, NULL);
    /* 仿真桩立即置 RUNNING + REVERSE → RELEASING */
    TEST_ASSERT_EQUAL(REAR_LOCK_STATE_RELEASING, rear_lock_state());
}

void test_rear_lock_release_direction_reverse(void)
{
    rear_lock_init(s_hexec, 0);
    rear_lock_release(0, NULL);
    TEST_ASSERT_EQUAL(MOTOR_DIR_REVERSE, motor_direction(&s_exec, 0));
}

void test_rear_lock_lock_with_spec(void)
{
    hal_motor_move_spec_t spec;
    memset(&spec, 0, sizeof(spec));
    spec.use_limit = true;
    spec.limit     = HAL_MOTOR_LIMIT_POS;

    rear_lock_init(s_hexec, 0);
    TEST_ASSERT_EQUAL(SW_OK, rear_lock_lock(0, &spec));
    TEST_ASSERT_EQUAL(REAR_LOCK_STATE_LOCKING, rear_lock_state());
}

void test_rear_lock_release_with_spec(void)
{
    hal_motor_move_spec_t spec;
    memset(&spec, 0, sizeof(spec));
    spec.use_limit = true;
    spec.limit     = HAL_MOTOR_LIMIT_NEG;

    rear_lock_init(s_hexec, 0);
    TEST_ASSERT_EQUAL(SW_OK, rear_lock_release(0, &spec));
    TEST_ASSERT_EQUAL(REAR_LOCK_STATE_RELEASING, rear_lock_state());
}

/* =========================================================================
 * D. 停止与回原点
 * ========================================================================= */

void test_rear_lock_stop_from_locking_ok(void)
{
    rear_lock_init(s_hexec, 0);
    rear_lock_lock(0, NULL);
    TEST_ASSERT_EQUAL(SW_OK, rear_lock_stop());
}

void test_rear_lock_stop_from_locking_goes_idle(void)
{
    rear_lock_init(s_hexec, 0);
    rear_lock_lock(0, NULL);
    TEST_ASSERT_EQUAL(REAR_LOCK_STATE_LOCKING, rear_lock_state());
    rear_lock_stop();
    /* 仿真桩 motor_stop 立即置 STOPPED → IDLE */
    TEST_ASSERT_EQUAL(REAR_LOCK_STATE_IDLE, rear_lock_state());
}

void test_rear_lock_stop_from_releasing_goes_idle(void)
{
    rear_lock_init(s_hexec, 0);
    rear_lock_release(0, NULL);
    rear_lock_stop();
    TEST_ASSERT_EQUAL(REAR_LOCK_STATE_IDLE, rear_lock_state());
}

void test_rear_lock_stop_from_idle_ok(void)
{
    rear_lock_init(s_hexec, 0);
    TEST_ASSERT_EQUAL(SW_OK, rear_lock_stop());
    TEST_ASSERT_EQUAL(REAR_LOCK_STATE_IDLE, rear_lock_state());
}

void test_rear_lock_home_returns_ok(void)
{
    rear_lock_init(s_hexec, 0);
    TEST_ASSERT_EQUAL(SW_OK, rear_lock_home());
}

void test_rear_lock_home_while_locking_goes_idle(void)
{
    rear_lock_init(s_hexec, 0);
    rear_lock_lock(0, NULL);
    TEST_ASSERT_EQUAL(REAR_LOCK_STATE_LOCKING, rear_lock_state());
    rear_lock_home();
    /* 仿真桩 motor_home 立即置 STOPPED */
    TEST_ASSERT_EQUAL(REAR_LOCK_STATE_IDLE, rear_lock_state());
}

/* =========================================================================
 * E. motor_phase → rear_lock_state 映射
 * ========================================================================= */

void test_rear_lock_state_paused_maps_to_idle(void)
{
    rear_lock_init(s_hexec, 0);
    s_exec.m[0].phase = MOTOR_PHASE_PAUSED;
    TEST_ASSERT_EQUAL(REAR_LOCK_STATE_IDLE, rear_lock_state());
}

void test_rear_lock_state_waiting_start_maps_to_idle(void)
{
    rear_lock_init(s_hexec, 0);
    s_exec.m[0].phase = MOTOR_PHASE_WAITING_START;
    TEST_ASSERT_EQUAL(REAR_LOCK_STATE_IDLE, rear_lock_state());
}

void test_rear_lock_state_running_forward_maps_to_locking(void)
{
    rear_lock_init(s_hexec, 0);
    s_exec.m[0].phase = MOTOR_PHASE_RUNNING;
    s_exec.m[0].dir   = MOTOR_DIR_FORWARD;
    TEST_ASSERT_EQUAL(REAR_LOCK_STATE_LOCKING, rear_lock_state());
}

void test_rear_lock_state_running_reverse_maps_to_releasing(void)
{
    rear_lock_init(s_hexec, 0);
    s_exec.m[0].phase = MOTOR_PHASE_RUNNING;
    s_exec.m[0].dir   = MOTOR_DIR_REVERSE;
    TEST_ASSERT_EQUAL(REAR_LOCK_STATE_RELEASING, rear_lock_state());
}

void test_rear_lock_state_decelerating_maps_to_stopping(void)
{
    rear_lock_init(s_hexec, 0);
    s_exec.m[0].phase = MOTOR_PHASE_DECELERATING;
    TEST_ASSERT_EQUAL(REAR_LOCK_STATE_STOPPING, rear_lock_state());
}

void test_rear_lock_state_reversal_wait_maps_to_stopping(void)
{
    rear_lock_init(s_hexec, 0);
    s_exec.m[0].phase = MOTOR_PHASE_REVERSAL_WAIT;
    TEST_ASSERT_EQUAL(REAR_LOCK_STATE_STOPPING, rear_lock_state());
}

void test_rear_lock_state_fault_maps_to_fault(void)
{
    rear_lock_init(s_hexec, 0);
    s_exec.m[0].phase = MOTOR_PHASE_FAULT;
    TEST_ASSERT_EQUAL(REAR_LOCK_STATE_FAULT, rear_lock_state());
}

void test_rear_lock_state_estop_maps_to_fault(void)
{
    rear_lock_init(s_hexec, 0);
    s_exec.m[0].phase = MOTOR_PHASE_ESTOP;
    TEST_ASSERT_EQUAL(REAR_LOCK_STATE_FAULT, rear_lock_state());
}

/* =========================================================================
 * F. 故障与恢复
 * ========================================================================= */

void test_rear_lock_fault_blocks_lock(void)
{
    rear_lock_init(s_hexec, 0);
    s_exec.m[0].phase = MOTOR_PHASE_FAULT;
    TEST_ASSERT_EQUAL(SW_ERR_STATE, rear_lock_lock(0, NULL));
}

void test_rear_lock_fault_blocks_release(void)
{
    rear_lock_init(s_hexec, 0);
    s_exec.m[0].phase = MOTOR_PHASE_FAULT;
    TEST_ASSERT_EQUAL(SW_ERR_STATE, rear_lock_release(0, NULL));
}

void test_rear_lock_fault_blocks_home(void)
{
    rear_lock_init(s_hexec, 0);
    s_exec.m[0].phase = MOTOR_PHASE_FAULT;
    TEST_ASSERT_EQUAL(SW_ERR_STATE, rear_lock_home());
}

void test_rear_lock_recover_module_stop_clears_fault(void)
{
    rear_lock_init(s_hexec, 0);
    s_exec.m[0].phase = MOTOR_PHASE_FAULT;
    TEST_ASSERT_EQUAL(SW_OK, rear_lock_recover(HAL_MOTOR_RECOVERY_MODULE_STOP));
    /* 仿真桩 RECOVERY_MODULE_STOP → phase = STOPPED → IDLE */
    TEST_ASSERT_EQUAL(REAR_LOCK_STATE_IDLE, rear_lock_state());
}

void test_rear_lock_recover_driver_reset_returns_ok(void)
{
    rear_lock_init(s_hexec, 0);
    s_exec.m[0].phase = MOTOR_PHASE_FAULT;
    TEST_ASSERT_EQUAL(SW_OK, rear_lock_recover(HAL_MOTOR_RECOVERY_DRIVER_RESET));
}

void test_rear_lock_fault_code_reflects_motor(void)
{
    rear_lock_init(s_hexec, 0);
    s_exec.m[0].phase      = MOTOR_PHASE_FAULT;
    s_exec.m[0].fault_code = MOTOR_FAULT_OVERCURRENT;
    TEST_ASSERT_EQUAL(MOTOR_FAULT_OVERCURRENT, rear_lock_fault_code());
}

void test_rear_lock_recover_then_lock_ok(void)
{
    rear_lock_init(s_hexec, 0);
    s_exec.m[0].phase = MOTOR_PHASE_FAULT;
    rear_lock_recover(HAL_MOTOR_RECOVERY_MODULE_STOP);
    TEST_ASSERT_EQUAL(SW_OK, rear_lock_lock(0, NULL));
    TEST_ASSERT_EQUAL(REAR_LOCK_STATE_LOCKING, rear_lock_state());
}

/* =========================================================================
 * main
 * ========================================================================= */

int main(void)
{
    UNITY_BEGIN();

    /* B. 未初始化保护（必须最先运行） */
    RUN_TEST(test_rear_lock_not_init_lock);
    RUN_TEST(test_rear_lock_not_init_release);
    RUN_TEST(test_rear_lock_not_init_stop);
    RUN_TEST(test_rear_lock_not_init_home);
    RUN_TEST(test_rear_lock_not_init_recover);
    RUN_TEST(test_rear_lock_not_init_state_idle);
    RUN_TEST(test_rear_lock_not_init_fault_code_none);

    /* A. 初始化参数校验 */
    RUN_TEST(test_rear_lock_init_null_returns_err_param);
    RUN_TEST(test_rear_lock_init_ok);
    RUN_TEST(test_rear_lock_state_idle_after_init);
    RUN_TEST(test_rear_lock_fault_code_none_after_init);

    /* C. 锁止 / 释放命令 */
    RUN_TEST(test_rear_lock_lock_returns_ok);
    RUN_TEST(test_rear_lock_lock_state_locking);
    RUN_TEST(test_rear_lock_lock_direction_forward);
    RUN_TEST(test_rear_lock_release_returns_ok);
    RUN_TEST(test_rear_lock_release_state_releasing);
    RUN_TEST(test_rear_lock_release_direction_reverse);
    RUN_TEST(test_rear_lock_lock_with_spec);
    RUN_TEST(test_rear_lock_release_with_spec);

    /* D. 停止与回原点 */
    RUN_TEST(test_rear_lock_stop_from_locking_ok);
    RUN_TEST(test_rear_lock_stop_from_locking_goes_idle);
    RUN_TEST(test_rear_lock_stop_from_releasing_goes_idle);
    RUN_TEST(test_rear_lock_stop_from_idle_ok);
    RUN_TEST(test_rear_lock_home_returns_ok);
    RUN_TEST(test_rear_lock_home_while_locking_goes_idle);

    /* E. motor_phase → rear_lock_state 映射 */
    RUN_TEST(test_rear_lock_state_paused_maps_to_idle);
    RUN_TEST(test_rear_lock_state_waiting_start_maps_to_idle);
    RUN_TEST(test_rear_lock_state_running_forward_maps_to_locking);
    RUN_TEST(test_rear_lock_state_running_reverse_maps_to_releasing);
    RUN_TEST(test_rear_lock_state_decelerating_maps_to_stopping);
    RUN_TEST(test_rear_lock_state_reversal_wait_maps_to_stopping);
    RUN_TEST(test_rear_lock_state_fault_maps_to_fault);
    RUN_TEST(test_rear_lock_state_estop_maps_to_fault);

    /* F. 故障与恢复 */
    RUN_TEST(test_rear_lock_fault_blocks_lock);
    RUN_TEST(test_rear_lock_fault_blocks_release);
    RUN_TEST(test_rear_lock_fault_blocks_home);
    RUN_TEST(test_rear_lock_recover_module_stop_clears_fault);
    RUN_TEST(test_rear_lock_recover_driver_reset_returns_ok);
    RUN_TEST(test_rear_lock_fault_code_reflects_motor);
    RUN_TEST(test_rear_lock_recover_then_lock_ok);

    return UNITY_END();
}
