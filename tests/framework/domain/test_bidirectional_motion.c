/**
 * @file    test_bidirectional_motion.c
 * @brief   bidirectional_motion 双向往复机构领域层单元测试
 *
 * 分组：
 *   B. 未初始化保护（须在 main() 中排在最前面，保证 s_motion.inited == false）
 *   A. 初始化参数校验
 *   C. 上升 / 下降命令（FORWARD=上升、REVERSE=下降）
 *   D. 停止与回原点
 *   E. motor_phase → lift_state 映射
 *   F. 故障与恢复
 *
 * @note    setUp() 只重置 motor_executor_t，不调用 bidirectional_motion_init()。
 *          各测试自行在函数体内调用 bidirectional_motion_init(&s_motion, s_hexec, 0, NULL) 以进入已初始化状态。
 *          未初始化测试（B 组）须最先运行，确保 s_motion 未初始化。
 */

#include "framework/domain/device_control/patterns/bidirectional_motion.h"
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
static bidirectional_motion_t s_motion;
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
    s_cfg.motors[0].default_max_move_ms  = 30000;
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
    memset(&s_motion, 0, sizeof(s_motion));
    /* 不在此调用 bidirectional_motion_init()，由各测试按需调用 */
}

void tearDown(void) {}

/* =========================================================================
 * B. 未初始化保护（列在 main() 最前面运行，此时 s_motion.inited == false）
 * ========================================================================= */

void test_bidir_not_init_up(void)
{
    TEST_ASSERT_EQUAL(SW_ERR_NOT_INIT, bidirectional_motion_run(&s_motion, HAL_MOTOR_DIR_FORWARD, 0, NULL));
}

void test_bidir_not_init_down(void)
{
    TEST_ASSERT_EQUAL(SW_ERR_NOT_INIT, bidirectional_motion_run(&s_motion, HAL_MOTOR_DIR_REVERSE, 0, NULL));
}

void test_bidir_not_init_stop(void)
{
    TEST_ASSERT_EQUAL(SW_ERR_NOT_INIT, bidirectional_motion_stop(&s_motion));
}

void test_bidir_not_init_home(void)
{
    TEST_ASSERT_EQUAL(SW_ERR_NOT_INIT, bidirectional_motion_home(&s_motion));
}

void test_bidir_not_init_position_zero(void)
{
    TEST_ASSERT_EQUAL_INT64(0, bidirectional_motion_position(&s_motion));
}

void test_bidir_not_init_recover(void)
{
    TEST_ASSERT_EQUAL(SW_ERR_NOT_INIT, bidirectional_motion_recover(&s_motion, HAL_MOTOR_RECOVERY_MODULE_STOP));
}

void test_bidir_not_init_state_idle(void)
{
    /* 未初始化时 bidirectional_motion_state(&s_motion) 应安全返回 IDLE */
    TEST_ASSERT_EQUAL(BIDIR_MOTION_STATE_IDLE, bidirectional_motion_state(&s_motion));
}

/* =========================================================================
 * A. 初始化参数校验
 * ========================================================================= */

void test_bidir_init_null_exec_returns_err_param(void)
{
    TEST_ASSERT_EQUAL(SW_ERR_PARAM, bidirectional_motion_init(&s_motion, NULL, 0, NULL));
}

void test_bidir_init_ok(void)
{
    TEST_ASSERT_EQUAL(SW_OK, bidirectional_motion_init(&s_motion, s_hexec, 0, NULL));
}

void test_bidir_state_idle_after_init(void)
{
    bidirectional_motion_init(&s_motion, s_hexec, 0, NULL);
    TEST_ASSERT_EQUAL(BIDIR_MOTION_STATE_IDLE, bidirectional_motion_state(&s_motion));
}

void test_bidir_position_zero_after_init(void)
{
    bidirectional_motion_init(&s_motion, s_hexec, 0, NULL);
    TEST_ASSERT_EQUAL_INT64(0, bidirectional_motion_position(&s_motion));
}

void test_bidir_fault_code_none_after_init(void)
{
    bidirectional_motion_init(&s_motion, s_hexec, 0, NULL);
    TEST_ASSERT_EQUAL(MOTOR_FAULT_NONE, bidirectional_motion_fault_code(&s_motion));
}

/* =========================================================================
 * C. 上升 / 下降命令（FORWARD = 上升，REVERSE = 下降）
 * ========================================================================= */

void test_bidir_up_returns_ok(void)
{
    bidirectional_motion_init(&s_motion, s_hexec, 0, NULL);
    TEST_ASSERT_EQUAL(SW_OK, bidirectional_motion_run(&s_motion, HAL_MOTOR_DIR_FORWARD, 0, NULL));
}

void test_bidir_up_state_lifting(void)
{
    bidirectional_motion_init(&s_motion, s_hexec, 0, NULL);
    bidirectional_motion_run(&s_motion, HAL_MOTOR_DIR_FORWARD, 0, NULL);
    /* 仿真桩 motor_run_continuous 立即置 RUNNING + FORWARD → LIFTING */
    TEST_ASSERT_EQUAL(BIDIR_MOTION_STATE_MOVING, bidirectional_motion_state(&s_motion));
}

void test_bidir_up_direction_forward(void)
{
    bidirectional_motion_init(&s_motion, s_hexec, 0, NULL);
    bidirectional_motion_run(&s_motion, HAL_MOTOR_DIR_FORWARD, 0, NULL);
    TEST_ASSERT_EQUAL(MOTOR_DIR_FORWARD, motor_direction(&s_exec, 0));
}

void test_bidir_down_returns_ok(void)
{
    bidirectional_motion_init(&s_motion, s_hexec, 0, NULL);
    TEST_ASSERT_EQUAL(SW_OK, bidirectional_motion_run(&s_motion, HAL_MOTOR_DIR_REVERSE, 0, NULL));
}

void test_bidir_down_state_lowering(void)
{
    bidirectional_motion_init(&s_motion, s_hexec, 0, NULL);
    bidirectional_motion_run(&s_motion, HAL_MOTOR_DIR_REVERSE, 0, NULL);
    /* 仿真桩立即置 RUNNING + REVERSE → LOWERING */
    TEST_ASSERT_EQUAL(BIDIR_MOTION_STATE_MOVING, bidirectional_motion_state(&s_motion));
}

void test_bidir_down_direction_reverse(void)
{
    bidirectional_motion_init(&s_motion, s_hexec, 0, NULL);
    bidirectional_motion_run(&s_motion, HAL_MOTOR_DIR_REVERSE, 0, NULL);
    TEST_ASSERT_EQUAL(MOTOR_DIR_REVERSE, motor_direction(&s_exec, 0));
}

void test_bidir_up_with_spec(void)
{
    hal_motor_move_spec_t spec;
    memset(&spec, 0, sizeof(spec));
    spec.use_limit = true;
    spec.limit     = HAL_MOTOR_LIMIT_POS;

    bidirectional_motion_init(&s_motion, s_hexec, 0, NULL);
    TEST_ASSERT_EQUAL(SW_OK, bidirectional_motion_run(&s_motion, HAL_MOTOR_DIR_FORWARD, 0, &spec));
    TEST_ASSERT_EQUAL(BIDIR_MOTION_STATE_MOVING, bidirectional_motion_state(&s_motion));
}

void test_bidir_down_with_spec(void)
{
    hal_motor_move_spec_t spec;
    memset(&spec, 0, sizeof(spec));
    spec.use_limit = true;
    spec.limit     = HAL_MOTOR_LIMIT_NEG;

    bidirectional_motion_init(&s_motion, s_hexec, 0, NULL);
    TEST_ASSERT_EQUAL(SW_OK, bidirectional_motion_run(&s_motion, HAL_MOTOR_DIR_REVERSE, 0, &spec));
    TEST_ASSERT_EQUAL(BIDIR_MOTION_STATE_MOVING, bidirectional_motion_state(&s_motion));
}

/* =========================================================================
 * D. 停止与回原点
 * ========================================================================= */

void test_bidir_stop_from_lifting_ok(void)
{
    bidirectional_motion_init(&s_motion, s_hexec, 0, NULL);
    bidirectional_motion_run(&s_motion, HAL_MOTOR_DIR_FORWARD, 0, NULL);
    TEST_ASSERT_EQUAL(SW_OK, bidirectional_motion_stop(&s_motion));
}

void test_bidir_stop_from_lifting_goes_idle(void)
{
    bidirectional_motion_init(&s_motion, s_hexec, 0, NULL);
    bidirectional_motion_run(&s_motion, HAL_MOTOR_DIR_FORWARD, 0, NULL);
    TEST_ASSERT_EQUAL(BIDIR_MOTION_STATE_MOVING, bidirectional_motion_state(&s_motion));
    bidirectional_motion_stop(&s_motion);
    /* 仿真桩 motor_stop 立即置 STOPPED → IDLE */
    TEST_ASSERT_EQUAL(BIDIR_MOTION_STATE_IDLE, bidirectional_motion_state(&s_motion));
}

void test_bidir_stop_from_idle_ok(void)
{
    bidirectional_motion_init(&s_motion, s_hexec, 0, NULL);
    TEST_ASSERT_EQUAL(SW_OK, bidirectional_motion_stop(&s_motion));
    TEST_ASSERT_EQUAL(BIDIR_MOTION_STATE_IDLE, bidirectional_motion_state(&s_motion));
}

void test_bidir_home_returns_ok(void)
{
    bidirectional_motion_init(&s_motion, s_hexec, 0, NULL);
    TEST_ASSERT_EQUAL(SW_OK, bidirectional_motion_home(&s_motion));
}

void test_bidir_home_while_lifting_goes_idle(void)
{
    bidirectional_motion_init(&s_motion, s_hexec, 0, NULL);
    bidirectional_motion_run(&s_motion, HAL_MOTOR_DIR_FORWARD, 0, NULL);
    TEST_ASSERT_EQUAL(BIDIR_MOTION_STATE_MOVING, bidirectional_motion_state(&s_motion));
    bidirectional_motion_home(&s_motion);
    /* 仿真桩 motor_home 立即置 STOPPED */
    TEST_ASSERT_EQUAL(BIDIR_MOTION_STATE_IDLE, bidirectional_motion_state(&s_motion));
}

void test_bidir_home_position_zero(void)
{
    bidirectional_motion_init(&s_motion, s_hexec, 0, NULL);
    /* 写入非零位置再归原点，验证位置清零 */
    s_exec.m[0].position = 9876;
    bidirectional_motion_home(&s_motion);
    TEST_ASSERT_EQUAL_INT64(0, bidirectional_motion_position(&s_motion));
}

/* =========================================================================
 * E. motor_phase → lift_state 映射
 * ========================================================================= */

void test_bidir_state_paused_maps_to_idle(void)
{
    bidirectional_motion_init(&s_motion, s_hexec, 0, NULL);
    s_exec.m[0].phase = MOTOR_PHASE_PAUSED;
    TEST_ASSERT_EQUAL(BIDIR_MOTION_STATE_IDLE, bidirectional_motion_state(&s_motion));
}

void test_bidir_state_waiting_start_maps_to_idle(void)
{
    bidirectional_motion_init(&s_motion, s_hexec, 0, NULL);
    s_exec.m[0].phase = MOTOR_PHASE_WAITING_START;
    TEST_ASSERT_EQUAL(BIDIR_MOTION_STATE_IDLE, bidirectional_motion_state(&s_motion));
}

void test_bidir_state_running_forward_maps_to_lifting(void)
{
    bidirectional_motion_init(&s_motion, s_hexec, 0, NULL);
    s_exec.m[0].phase = MOTOR_PHASE_RUNNING;
    s_exec.m[0].dir   = MOTOR_DIR_FORWARD;
    TEST_ASSERT_EQUAL(BIDIR_MOTION_STATE_MOVING, bidirectional_motion_state(&s_motion));
}

void test_bidir_state_running_reverse_maps_to_lowering(void)
{
    bidirectional_motion_init(&s_motion, s_hexec, 0, NULL);
    s_exec.m[0].phase = MOTOR_PHASE_RUNNING;
    s_exec.m[0].dir   = MOTOR_DIR_REVERSE;
    TEST_ASSERT_EQUAL(BIDIR_MOTION_STATE_MOVING, bidirectional_motion_state(&s_motion));
}

void test_bidir_state_decelerating_maps_to_stopping(void)
{
    bidirectional_motion_init(&s_motion, s_hexec, 0, NULL);
    s_exec.m[0].phase = MOTOR_PHASE_DECELERATING;
    TEST_ASSERT_EQUAL(BIDIR_MOTION_STATE_STOPPING, bidirectional_motion_state(&s_motion));
}

void test_bidir_state_reversal_wait_maps_to_stopping(void)
{
    bidirectional_motion_init(&s_motion, s_hexec, 0, NULL);
    s_exec.m[0].phase = MOTOR_PHASE_REVERSAL_WAIT;
    TEST_ASSERT_EQUAL(BIDIR_MOTION_STATE_STOPPING, bidirectional_motion_state(&s_motion));
}

void test_bidir_state_fault_maps_to_fault(void)
{
    bidirectional_motion_init(&s_motion, s_hexec, 0, NULL);
    s_exec.m[0].phase = MOTOR_PHASE_FAULT;
    TEST_ASSERT_EQUAL(BIDIR_MOTION_STATE_FAULT, bidirectional_motion_state(&s_motion));
}

void test_bidir_state_estop_maps_to_fault(void)
{
    bidirectional_motion_init(&s_motion, s_hexec, 0, NULL);
    s_exec.m[0].phase = MOTOR_PHASE_ESTOP;
    TEST_ASSERT_EQUAL(BIDIR_MOTION_STATE_FAULT, bidirectional_motion_state(&s_motion));
}

/* =========================================================================
 * F. 故障与恢复
 * ========================================================================= */

void test_bidir_fault_blocks_up(void)
{
    bidirectional_motion_init(&s_motion, s_hexec, 0, NULL);
    s_exec.m[0].phase = MOTOR_PHASE_FAULT;
    TEST_ASSERT_EQUAL(SW_ERR_STATE, bidirectional_motion_run(&s_motion, HAL_MOTOR_DIR_FORWARD, 0, NULL));
}

void test_bidir_fault_blocks_down(void)
{
    bidirectional_motion_init(&s_motion, s_hexec, 0, NULL);
    s_exec.m[0].phase = MOTOR_PHASE_FAULT;
    TEST_ASSERT_EQUAL(SW_ERR_STATE, bidirectional_motion_run(&s_motion, HAL_MOTOR_DIR_REVERSE, 0, NULL));
}

void test_bidir_fault_blocks_home(void)
{
    bidirectional_motion_init(&s_motion, s_hexec, 0, NULL);
    s_exec.m[0].phase = MOTOR_PHASE_FAULT;
    TEST_ASSERT_EQUAL(SW_ERR_STATE, bidirectional_motion_home(&s_motion));
}

void test_bidir_recover_module_stop_clears_fault(void)
{
    bidirectional_motion_init(&s_motion, s_hexec, 0, NULL);
    s_exec.m[0].phase = MOTOR_PHASE_FAULT;
    TEST_ASSERT_EQUAL(SW_OK, bidirectional_motion_recover(&s_motion, HAL_MOTOR_RECOVERY_MODULE_STOP));
    /* 仿真桩 RECOVERY_MODULE_STOP → phase = STOPPED → IDLE */
    TEST_ASSERT_EQUAL(BIDIR_MOTION_STATE_IDLE, bidirectional_motion_state(&s_motion));
}

void test_bidir_recover_driver_reset_returns_ok(void)
{
    bidirectional_motion_init(&s_motion, s_hexec, 0, NULL);
    s_exec.m[0].phase = MOTOR_PHASE_FAULT;
    TEST_ASSERT_EQUAL(SW_OK, bidirectional_motion_recover(&s_motion, HAL_MOTOR_RECOVERY_DRIVER_RESET));
}

void test_bidir_fault_code_reflects_motor(void)
{
    bidirectional_motion_init(&s_motion, s_hexec, 0, NULL);
    s_exec.m[0].phase      = MOTOR_PHASE_FAULT;
    s_exec.m[0].fault_code = MOTOR_FAULT_OVERCURRENT;
    TEST_ASSERT_EQUAL(MOTOR_FAULT_OVERCURRENT, bidirectional_motion_fault_code(&s_motion));
}

void test_bidir_recover_then_up_ok(void)
{
    bidirectional_motion_init(&s_motion, s_hexec, 0, NULL);
    s_exec.m[0].phase = MOTOR_PHASE_FAULT;
    bidirectional_motion_recover(&s_motion, HAL_MOTOR_RECOVERY_MODULE_STOP);
    TEST_ASSERT_EQUAL(SW_OK, bidirectional_motion_run(&s_motion, HAL_MOTOR_DIR_FORWARD, 0, NULL));
    TEST_ASSERT_EQUAL(BIDIR_MOTION_STATE_MOVING, bidirectional_motion_state(&s_motion));
}

/* =========================================================================
 * main
 * ========================================================================= */

int main(void)
{
    UNITY_BEGIN();

    /* B. 未初始化保护（必须最先运行） */
    RUN_TEST(test_bidir_not_init_up);
    RUN_TEST(test_bidir_not_init_down);
    RUN_TEST(test_bidir_not_init_stop);
    RUN_TEST(test_bidir_not_init_home);
    RUN_TEST(test_bidir_not_init_position_zero);
    RUN_TEST(test_bidir_not_init_recover);
    RUN_TEST(test_bidir_not_init_state_idle);

    /* A. 初始化参数校验 */
    RUN_TEST(test_bidir_init_null_exec_returns_err_param);
    RUN_TEST(test_bidir_init_ok);
    RUN_TEST(test_bidir_state_idle_after_init);
    RUN_TEST(test_bidir_position_zero_after_init);
    RUN_TEST(test_bidir_fault_code_none_after_init);

    /* C. 上升 / 下降命令 */
    RUN_TEST(test_bidir_up_returns_ok);
    RUN_TEST(test_bidir_up_state_lifting);
    RUN_TEST(test_bidir_up_direction_forward);
    RUN_TEST(test_bidir_down_returns_ok);
    RUN_TEST(test_bidir_down_state_lowering);
    RUN_TEST(test_bidir_down_direction_reverse);
    RUN_TEST(test_bidir_up_with_spec);
    RUN_TEST(test_bidir_down_with_spec);

    /* D. 停止与回原点 */
    RUN_TEST(test_bidir_stop_from_lifting_ok);
    RUN_TEST(test_bidir_stop_from_lifting_goes_idle);
    RUN_TEST(test_bidir_stop_from_idle_ok);
    RUN_TEST(test_bidir_home_returns_ok);
    RUN_TEST(test_bidir_home_while_lifting_goes_idle);
    RUN_TEST(test_bidir_home_position_zero);

    /* E. motor_phase → lift_state 映射 */
    RUN_TEST(test_bidir_state_paused_maps_to_idle);
    RUN_TEST(test_bidir_state_waiting_start_maps_to_idle);
    RUN_TEST(test_bidir_state_running_forward_maps_to_lifting);
    RUN_TEST(test_bidir_state_running_reverse_maps_to_lowering);
    RUN_TEST(test_bidir_state_decelerating_maps_to_stopping);
    RUN_TEST(test_bidir_state_reversal_wait_maps_to_stopping);
    RUN_TEST(test_bidir_state_fault_maps_to_fault);
    RUN_TEST(test_bidir_state_estop_maps_to_fault);

    /* F. 故障与恢复 */
    RUN_TEST(test_bidir_fault_blocks_up);
    RUN_TEST(test_bidir_fault_blocks_down);
    RUN_TEST(test_bidir_fault_blocks_home);
    RUN_TEST(test_bidir_recover_module_stop_clears_fault);
    RUN_TEST(test_bidir_recover_driver_reset_returns_ok);
    RUN_TEST(test_bidir_fault_code_reflects_motor);
    RUN_TEST(test_bidir_recover_then_up_ok);

    return UNITY_END();
}
