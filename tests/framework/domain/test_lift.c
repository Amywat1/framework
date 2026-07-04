/**
 * @file    test_lift.c
 * @brief   lift 顶刷升降机构领域层单元测试
 *
 * 分组：
 *   B. 未初始化保护（须在 main() 中排在最前面，保证 lift.c s_exec == NULL）
 *   A. 初始化参数校验
 *   C. 上升 / 下降命令（FORWARD=上升、REVERSE=下降）
 *   D. 停止与回原点
 *   E. motor_phase → lift_state 映射
 *   F. 故障与恢复
 *
 * @note    setUp() 只重置 motor_executor_t，不调用 lift_init()。
 *          各测试自行在函数体内调用 lift_init(s_hexec, 0) 以进入已初始化状态。
 *          未初始化测试（B 组）须最先运行，确保 lift.c 内 s_exec == NULL。
 */

#include "framework/domain/device_control/mechanism/lift.h"
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
    /* 不在此调用 lift_init()，由各测试按需调用 */
}

void tearDown(void) {}

/* =========================================================================
 * B. 未初始化保护（列在 main() 最前面运行，此时 lift.c s_exec == NULL）
 * ========================================================================= */

void test_lift_not_init_up(void)
{
    TEST_ASSERT_EQUAL(SW_ERR_NOT_INIT, lift_up(0, NULL));
}

void test_lift_not_init_down(void)
{
    TEST_ASSERT_EQUAL(SW_ERR_NOT_INIT, lift_down(0, NULL));
}

void test_lift_not_init_stop(void)
{
    TEST_ASSERT_EQUAL(SW_ERR_NOT_INIT, lift_stop());
}

void test_lift_not_init_home(void)
{
    TEST_ASSERT_EQUAL(SW_ERR_NOT_INIT, lift_home());
}

void test_lift_not_init_position_zero(void)
{
    TEST_ASSERT_EQUAL_INT64(0, lift_position());
}

void test_lift_not_init_recover(void)
{
    TEST_ASSERT_EQUAL(SW_ERR_NOT_INIT, lift_recover(HAL_MOTOR_RECOVERY_MODULE_STOP));
}

void test_lift_not_init_state_idle(void)
{
    /* 未初始化时 lift_state() 应安全返回 IDLE */
    TEST_ASSERT_EQUAL(LIFT_STATE_IDLE, lift_state());
}

/* =========================================================================
 * A. 初始化参数校验
 * ========================================================================= */

void test_lift_init_null_returns_err_param(void)
{
    TEST_ASSERT_EQUAL(SW_ERR_PARAM, lift_init(NULL, 0));
}

void test_lift_init_ok(void)
{
    TEST_ASSERT_EQUAL(SW_OK, lift_init(s_hexec, 0));
}

void test_lift_state_idle_after_init(void)
{
    lift_init(s_hexec, 0);
    TEST_ASSERT_EQUAL(LIFT_STATE_IDLE, lift_state());
}

void test_lift_position_zero_after_init(void)
{
    lift_init(s_hexec, 0);
    TEST_ASSERT_EQUAL_INT64(0, lift_position());
}

void test_lift_fault_code_none_after_init(void)
{
    lift_init(s_hexec, 0);
    TEST_ASSERT_EQUAL(MOTOR_FAULT_NONE, lift_fault_code());
}

/* =========================================================================
 * C. 上升 / 下降命令（FORWARD = 上升，REVERSE = 下降）
 * ========================================================================= */

void test_lift_up_returns_ok(void)
{
    lift_init(s_hexec, 0);
    TEST_ASSERT_EQUAL(SW_OK, lift_up(0, NULL));
}

void test_lift_up_state_lifting(void)
{
    lift_init(s_hexec, 0);
    lift_up(0, NULL);
    /* 仿真桩 motor_run_continuous 立即置 RUNNING + FORWARD → LIFTING */
    TEST_ASSERT_EQUAL(LIFT_STATE_LIFTING, lift_state());
}

void test_lift_up_direction_forward(void)
{
    lift_init(s_hexec, 0);
    lift_up(0, NULL);
    TEST_ASSERT_EQUAL(MOTOR_DIR_FORWARD, motor_direction(&s_exec, 0));
}

void test_lift_down_returns_ok(void)
{
    lift_init(s_hexec, 0);
    TEST_ASSERT_EQUAL(SW_OK, lift_down(0, NULL));
}

void test_lift_down_state_lowering(void)
{
    lift_init(s_hexec, 0);
    lift_down(0, NULL);
    /* 仿真桩立即置 RUNNING + REVERSE → LOWERING */
    TEST_ASSERT_EQUAL(LIFT_STATE_LOWERING, lift_state());
}

void test_lift_down_direction_reverse(void)
{
    lift_init(s_hexec, 0);
    lift_down(0, NULL);
    TEST_ASSERT_EQUAL(MOTOR_DIR_REVERSE, motor_direction(&s_exec, 0));
}

void test_lift_up_with_spec(void)
{
    hal_motor_move_spec_t spec;
    memset(&spec, 0, sizeof(spec));
    spec.use_limit = true;
    spec.limit     = HAL_MOTOR_LIMIT_POS;

    lift_init(s_hexec, 0);
    TEST_ASSERT_EQUAL(SW_OK, lift_up(0, &spec));
    TEST_ASSERT_EQUAL(LIFT_STATE_LIFTING, lift_state());
}

void test_lift_down_with_spec(void)
{
    hal_motor_move_spec_t spec;
    memset(&spec, 0, sizeof(spec));
    spec.use_limit = true;
    spec.limit     = HAL_MOTOR_LIMIT_NEG;

    lift_init(s_hexec, 0);
    TEST_ASSERT_EQUAL(SW_OK, lift_down(0, &spec));
    TEST_ASSERT_EQUAL(LIFT_STATE_LOWERING, lift_state());
}

/* =========================================================================
 * D. 停止与回原点
 * ========================================================================= */

void test_lift_stop_from_lifting_ok(void)
{
    lift_init(s_hexec, 0);
    lift_up(0, NULL);
    TEST_ASSERT_EQUAL(SW_OK, lift_stop());
}

void test_lift_stop_from_lifting_goes_idle(void)
{
    lift_init(s_hexec, 0);
    lift_up(0, NULL);
    TEST_ASSERT_EQUAL(LIFT_STATE_LIFTING, lift_state());
    lift_stop();
    /* 仿真桩 motor_stop 立即置 STOPPED → IDLE */
    TEST_ASSERT_EQUAL(LIFT_STATE_IDLE, lift_state());
}

void test_lift_stop_from_idle_ok(void)
{
    lift_init(s_hexec, 0);
    TEST_ASSERT_EQUAL(SW_OK, lift_stop());
    TEST_ASSERT_EQUAL(LIFT_STATE_IDLE, lift_state());
}

void test_lift_home_returns_ok(void)
{
    lift_init(s_hexec, 0);
    TEST_ASSERT_EQUAL(SW_OK, lift_home());
}

void test_lift_home_while_lifting_goes_idle(void)
{
    lift_init(s_hexec, 0);
    lift_up(0, NULL);
    TEST_ASSERT_EQUAL(LIFT_STATE_LIFTING, lift_state());
    lift_home();
    /* 仿真桩 motor_home 立即置 STOPPED */
    TEST_ASSERT_EQUAL(LIFT_STATE_IDLE, lift_state());
}

void test_lift_home_position_zero(void)
{
    lift_init(s_hexec, 0);
    /* 写入非零位置再归原点，验证位置清零 */
    s_exec.m[0].position = 9876;
    lift_home();
    TEST_ASSERT_EQUAL_INT64(0, lift_position());
}

/* =========================================================================
 * E. motor_phase → lift_state 映射
 * ========================================================================= */

void test_lift_state_paused_maps_to_idle(void)
{
    lift_init(s_hexec, 0);
    s_exec.m[0].phase = MOTOR_PHASE_PAUSED;
    TEST_ASSERT_EQUAL(LIFT_STATE_IDLE, lift_state());
}

void test_lift_state_waiting_start_maps_to_idle(void)
{
    lift_init(s_hexec, 0);
    s_exec.m[0].phase = MOTOR_PHASE_WAITING_START;
    TEST_ASSERT_EQUAL(LIFT_STATE_IDLE, lift_state());
}

void test_lift_state_running_forward_maps_to_lifting(void)
{
    lift_init(s_hexec, 0);
    s_exec.m[0].phase = MOTOR_PHASE_RUNNING;
    s_exec.m[0].dir   = MOTOR_DIR_FORWARD;
    TEST_ASSERT_EQUAL(LIFT_STATE_LIFTING, lift_state());
}

void test_lift_state_running_reverse_maps_to_lowering(void)
{
    lift_init(s_hexec, 0);
    s_exec.m[0].phase = MOTOR_PHASE_RUNNING;
    s_exec.m[0].dir   = MOTOR_DIR_REVERSE;
    TEST_ASSERT_EQUAL(LIFT_STATE_LOWERING, lift_state());
}

void test_lift_state_decelerating_maps_to_stopping(void)
{
    lift_init(s_hexec, 0);
    s_exec.m[0].phase = MOTOR_PHASE_DECELERATING;
    TEST_ASSERT_EQUAL(LIFT_STATE_STOPPING, lift_state());
}

void test_lift_state_reversal_wait_maps_to_stopping(void)
{
    lift_init(s_hexec, 0);
    s_exec.m[0].phase = MOTOR_PHASE_REVERSAL_WAIT;
    TEST_ASSERT_EQUAL(LIFT_STATE_STOPPING, lift_state());
}

void test_lift_state_fault_maps_to_fault(void)
{
    lift_init(s_hexec, 0);
    s_exec.m[0].phase = MOTOR_PHASE_FAULT;
    TEST_ASSERT_EQUAL(LIFT_STATE_FAULT, lift_state());
}

void test_lift_state_estop_maps_to_fault(void)
{
    lift_init(s_hexec, 0);
    s_exec.m[0].phase = MOTOR_PHASE_ESTOP;
    TEST_ASSERT_EQUAL(LIFT_STATE_FAULT, lift_state());
}

/* =========================================================================
 * F. 故障与恢复
 * ========================================================================= */

void test_lift_fault_blocks_up(void)
{
    lift_init(s_hexec, 0);
    s_exec.m[0].phase = MOTOR_PHASE_FAULT;
    TEST_ASSERT_EQUAL(SW_ERR_STATE, lift_up(0, NULL));
}

void test_lift_fault_blocks_down(void)
{
    lift_init(s_hexec, 0);
    s_exec.m[0].phase = MOTOR_PHASE_FAULT;
    TEST_ASSERT_EQUAL(SW_ERR_STATE, lift_down(0, NULL));
}

void test_lift_fault_blocks_home(void)
{
    lift_init(s_hexec, 0);
    s_exec.m[0].phase = MOTOR_PHASE_FAULT;
    TEST_ASSERT_EQUAL(SW_ERR_STATE, lift_home());
}

void test_lift_recover_module_stop_clears_fault(void)
{
    lift_init(s_hexec, 0);
    s_exec.m[0].phase = MOTOR_PHASE_FAULT;
    TEST_ASSERT_EQUAL(SW_OK, lift_recover(HAL_MOTOR_RECOVERY_MODULE_STOP));
    /* 仿真桩 RECOVERY_MODULE_STOP → phase = STOPPED → IDLE */
    TEST_ASSERT_EQUAL(LIFT_STATE_IDLE, lift_state());
}

void test_lift_recover_driver_reset_returns_ok(void)
{
    lift_init(s_hexec, 0);
    s_exec.m[0].phase = MOTOR_PHASE_FAULT;
    TEST_ASSERT_EQUAL(SW_OK, lift_recover(HAL_MOTOR_RECOVERY_DRIVER_RESET));
}

void test_lift_fault_code_reflects_motor(void)
{
    lift_init(s_hexec, 0);
    s_exec.m[0].phase      = MOTOR_PHASE_FAULT;
    s_exec.m[0].fault_code = MOTOR_FAULT_OVERCURRENT;
    TEST_ASSERT_EQUAL(MOTOR_FAULT_OVERCURRENT, lift_fault_code());
}

void test_lift_recover_then_up_ok(void)
{
    lift_init(s_hexec, 0);
    s_exec.m[0].phase = MOTOR_PHASE_FAULT;
    lift_recover(HAL_MOTOR_RECOVERY_MODULE_STOP);
    TEST_ASSERT_EQUAL(SW_OK, lift_up(0, NULL));
    TEST_ASSERT_EQUAL(LIFT_STATE_LIFTING, lift_state());
}

/* =========================================================================
 * main
 * ========================================================================= */

int main(void)
{
    UNITY_BEGIN();

    /* B. 未初始化保护（必须最先运行） */
    RUN_TEST(test_lift_not_init_up);
    RUN_TEST(test_lift_not_init_down);
    RUN_TEST(test_lift_not_init_stop);
    RUN_TEST(test_lift_not_init_home);
    RUN_TEST(test_lift_not_init_position_zero);
    RUN_TEST(test_lift_not_init_recover);
    RUN_TEST(test_lift_not_init_state_idle);

    /* A. 初始化参数校验 */
    RUN_TEST(test_lift_init_null_returns_err_param);
    RUN_TEST(test_lift_init_ok);
    RUN_TEST(test_lift_state_idle_after_init);
    RUN_TEST(test_lift_position_zero_after_init);
    RUN_TEST(test_lift_fault_code_none_after_init);

    /* C. 上升 / 下降命令 */
    RUN_TEST(test_lift_up_returns_ok);
    RUN_TEST(test_lift_up_state_lifting);
    RUN_TEST(test_lift_up_direction_forward);
    RUN_TEST(test_lift_down_returns_ok);
    RUN_TEST(test_lift_down_state_lowering);
    RUN_TEST(test_lift_down_direction_reverse);
    RUN_TEST(test_lift_up_with_spec);
    RUN_TEST(test_lift_down_with_spec);

    /* D. 停止与回原点 */
    RUN_TEST(test_lift_stop_from_lifting_ok);
    RUN_TEST(test_lift_stop_from_lifting_goes_idle);
    RUN_TEST(test_lift_stop_from_idle_ok);
    RUN_TEST(test_lift_home_returns_ok);
    RUN_TEST(test_lift_home_while_lifting_goes_idle);
    RUN_TEST(test_lift_home_position_zero);

    /* E. motor_phase → lift_state 映射 */
    RUN_TEST(test_lift_state_paused_maps_to_idle);
    RUN_TEST(test_lift_state_waiting_start_maps_to_idle);
    RUN_TEST(test_lift_state_running_forward_maps_to_lifting);
    RUN_TEST(test_lift_state_running_reverse_maps_to_lowering);
    RUN_TEST(test_lift_state_decelerating_maps_to_stopping);
    RUN_TEST(test_lift_state_reversal_wait_maps_to_stopping);
    RUN_TEST(test_lift_state_fault_maps_to_fault);
    RUN_TEST(test_lift_state_estop_maps_to_fault);

    /* F. 故障与恢复 */
    RUN_TEST(test_lift_fault_blocks_up);
    RUN_TEST(test_lift_fault_blocks_down);
    RUN_TEST(test_lift_fault_blocks_home);
    RUN_TEST(test_lift_recover_module_stop_clears_fault);
    RUN_TEST(test_lift_recover_driver_reset_returns_ok);
    RUN_TEST(test_lift_fault_code_reflects_motor);
    RUN_TEST(test_lift_recover_then_up_ok);

    return UNITY_END();
}
