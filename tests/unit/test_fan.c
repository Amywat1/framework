/**
 * @file    test_fan.c
 * @brief   fan 风机机构领域层单元测试
 *
 * 分组：
 *   B. 未初始化保护（须在 main() 中排在最前面，保证 fan.c s_exec == NULL）
 *   A. 初始化参数校验
 *   C. 启动与停止（IDLE ↔ RUNNING）
 *   D. motor_phase → fan_state 映射
 *   E. 故障与恢复
 *
 * @note    setUp() 只重置 motor_executor_t，不调用 fan_init()。
 *          各测试自行在函数体内调用 fan_init(&s_exec, 0) 以进入已初始化状态。
 *          未初始化测试（B 组）须最先运行，确保 fan.c 内 s_exec == NULL。
 */

#include "domain/device/mechanism/fan.h"
#include "motor/motor_executor.h"
#include "common/sw_error.h"
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
    /* 不在此调用 fan_init()，由各测试按需调用 */
}

void tearDown(void) {}

/* =========================================================================
 * B. 未初始化保护（列在 main() 最前面运行，此时 fan.c s_exec == NULL）
 * ========================================================================= */

void test_fan_not_init_start(void)
{
    TEST_ASSERT_EQUAL(SW_ERR_NOT_INIT, fan_start());
}

void test_fan_not_init_stop(void)
{
    TEST_ASSERT_EQUAL(SW_ERR_NOT_INIT, fan_stop());
}

void test_fan_not_init_recover(void)
{
    TEST_ASSERT_EQUAL(SW_ERR_NOT_INIT, fan_recover(MOTOR_RECOVERY_MODULE_STOP));
}

void test_fan_not_init_state_idle(void)
{
    /* 未初始化时 fan_state() 应安全返回 IDLE */
    TEST_ASSERT_EQUAL(FAN_STATE_IDLE, fan_state());
}

void test_fan_not_init_fault_code_none(void)
{
    TEST_ASSERT_EQUAL(MOTOR_FAULT_NONE, fan_fault_code());
}

/* =========================================================================
 * A. 初始化参数校验
 * ========================================================================= */

void test_fan_init_null_returns_err_param(void)
{
    TEST_ASSERT_EQUAL(SW_ERR_PARAM, fan_init(NULL, 0));
}

void test_fan_init_ok(void)
{
    TEST_ASSERT_EQUAL(SW_OK, fan_init(&s_exec, 0));
}

void test_fan_state_idle_after_init(void)
{
    fan_init(&s_exec, 0);
    TEST_ASSERT_EQUAL(FAN_STATE_IDLE, fan_state());
}

void test_fan_fault_code_none_after_init(void)
{
    fan_init(&s_exec, 0);
    TEST_ASSERT_EQUAL(MOTOR_FAULT_NONE, fan_fault_code());
}

/* =========================================================================
 * C. 启动与停止（IDLE ↔ RUNNING）
 * ========================================================================= */

void test_fan_start_returns_ok(void)
{
    fan_init(&s_exec, 0);
    TEST_ASSERT_EQUAL(SW_OK, fan_start());
}

void test_fan_start_state_running(void)
{
    fan_init(&s_exec, 0);
    fan_start();
    /* 仿真桩 motor_run_continuous 立即置 RUNNING */
    TEST_ASSERT_EQUAL(FAN_STATE_RUNNING, fan_state());
}

void test_fan_start_direction_forward(void)
{
    fan_init(&s_exec, 0);
    fan_start();
    TEST_ASSERT_EQUAL(MOTOR_DIR_FORWARD, motor_direction(&s_exec, 0));
}

void test_fan_stop_returns_ok(void)
{
    fan_init(&s_exec, 0);
    fan_start();
    TEST_ASSERT_EQUAL(SW_OK, fan_stop());
}

void test_fan_stop_state_idle(void)
{
    fan_init(&s_exec, 0);
    fan_start();
    TEST_ASSERT_EQUAL(FAN_STATE_RUNNING, fan_state());
    fan_stop();
    /* 仿真桩 motor_stop 立即置 STOPPED → IDLE */
    TEST_ASSERT_EQUAL(FAN_STATE_IDLE, fan_state());
}

void test_fan_stop_from_idle_ok(void)
{
    fan_init(&s_exec, 0);
    TEST_ASSERT_EQUAL(SW_OK, fan_stop());
    TEST_ASSERT_EQUAL(FAN_STATE_IDLE, fan_state());
}

/* =========================================================================
 * D. motor_phase → fan_state 映射
 * ========================================================================= */

void test_fan_state_paused_maps_to_idle(void)
{
    fan_init(&s_exec, 0);
    s_exec.m[0].phase = MOTOR_PHASE_PAUSED;
    TEST_ASSERT_EQUAL(FAN_STATE_IDLE, fan_state());
}

void test_fan_state_waiting_start_maps_to_idle(void)
{
    fan_init(&s_exec, 0);
    s_exec.m[0].phase = MOTOR_PHASE_WAITING_START;
    TEST_ASSERT_EQUAL(FAN_STATE_IDLE, fan_state());
}

void test_fan_state_running_maps_to_running(void)
{
    fan_init(&s_exec, 0);
    s_exec.m[0].phase = MOTOR_PHASE_RUNNING;
    TEST_ASSERT_EQUAL(FAN_STATE_RUNNING, fan_state());
}

void test_fan_state_decelerating_maps_to_stopping(void)
{
    fan_init(&s_exec, 0);
    s_exec.m[0].phase = MOTOR_PHASE_DECELERATING;
    TEST_ASSERT_EQUAL(FAN_STATE_STOPPING, fan_state());
}

void test_fan_state_reversal_wait_maps_to_stopping(void)
{
    fan_init(&s_exec, 0);
    s_exec.m[0].phase = MOTOR_PHASE_REVERSAL_WAIT;
    TEST_ASSERT_EQUAL(FAN_STATE_STOPPING, fan_state());
}

void test_fan_state_fault_maps_to_fault(void)
{
    fan_init(&s_exec, 0);
    s_exec.m[0].phase = MOTOR_PHASE_FAULT;
    TEST_ASSERT_EQUAL(FAN_STATE_FAULT, fan_state());
}

void test_fan_state_estop_maps_to_fault(void)
{
    fan_init(&s_exec, 0);
    s_exec.m[0].phase = MOTOR_PHASE_ESTOP;
    TEST_ASSERT_EQUAL(FAN_STATE_FAULT, fan_state());
}

/* =========================================================================
 * E. 故障与恢复
 * ========================================================================= */

void test_fan_fault_blocks_start(void)
{
    fan_init(&s_exec, 0);
    s_exec.m[0].phase = MOTOR_PHASE_FAULT;
    TEST_ASSERT_EQUAL(SW_ERR_STATE, fan_start());
}

void test_fan_recover_module_stop_clears_fault(void)
{
    fan_init(&s_exec, 0);
    s_exec.m[0].phase = MOTOR_PHASE_FAULT;
    TEST_ASSERT_EQUAL(SW_OK, fan_recover(MOTOR_RECOVERY_MODULE_STOP));
    /* 仿真桩 RECOVERY_MODULE_STOP → phase = STOPPED → IDLE */
    TEST_ASSERT_EQUAL(FAN_STATE_IDLE, fan_state());
}

void test_fan_recover_driver_reset_returns_ok(void)
{
    fan_init(&s_exec, 0);
    s_exec.m[0].phase = MOTOR_PHASE_FAULT;
    TEST_ASSERT_EQUAL(SW_OK, fan_recover(MOTOR_RECOVERY_DRIVER_RESET));
}

void test_fan_fault_code_reflects_motor(void)
{
    fan_init(&s_exec, 0);
    s_exec.m[0].phase      = MOTOR_PHASE_FAULT;
    s_exec.m[0].fault_code = MOTOR_FAULT_OVERCURRENT;
    TEST_ASSERT_EQUAL(MOTOR_FAULT_OVERCURRENT, fan_fault_code());
}

void test_fan_recover_then_start_ok(void)
{
    fan_init(&s_exec, 0);
    s_exec.m[0].phase = MOTOR_PHASE_FAULT;
    fan_recover(MOTOR_RECOVERY_MODULE_STOP);
    TEST_ASSERT_EQUAL(SW_OK, fan_start());
    TEST_ASSERT_EQUAL(FAN_STATE_RUNNING, fan_state());
}

/* =========================================================================
 * main
 * ========================================================================= */

int main(void)
{
    UNITY_BEGIN();

    /* B. 未初始化保护（必须最先运行） */
    RUN_TEST(test_fan_not_init_start);
    RUN_TEST(test_fan_not_init_stop);
    RUN_TEST(test_fan_not_init_recover);
    RUN_TEST(test_fan_not_init_state_idle);
    RUN_TEST(test_fan_not_init_fault_code_none);

    /* A. 初始化参数校验 */
    RUN_TEST(test_fan_init_null_returns_err_param);
    RUN_TEST(test_fan_init_ok);
    RUN_TEST(test_fan_state_idle_after_init);
    RUN_TEST(test_fan_fault_code_none_after_init);

    /* C. 启动与停止 */
    RUN_TEST(test_fan_start_returns_ok);
    RUN_TEST(test_fan_start_state_running);
    RUN_TEST(test_fan_start_direction_forward);
    RUN_TEST(test_fan_stop_returns_ok);
    RUN_TEST(test_fan_stop_state_idle);
    RUN_TEST(test_fan_stop_from_idle_ok);

    /* D. motor_phase → fan_state 映射 */
    RUN_TEST(test_fan_state_paused_maps_to_idle);
    RUN_TEST(test_fan_state_waiting_start_maps_to_idle);
    RUN_TEST(test_fan_state_running_maps_to_running);
    RUN_TEST(test_fan_state_decelerating_maps_to_stopping);
    RUN_TEST(test_fan_state_reversal_wait_maps_to_stopping);
    RUN_TEST(test_fan_state_fault_maps_to_fault);
    RUN_TEST(test_fan_state_estop_maps_to_fault);

    /* E. 故障与恢复 */
    RUN_TEST(test_fan_fault_blocks_start);
    RUN_TEST(test_fan_recover_module_stop_clears_fault);
    RUN_TEST(test_fan_recover_driver_reset_returns_ok);
    RUN_TEST(test_fan_fault_code_reflects_motor);
    RUN_TEST(test_fan_recover_then_start_ok);

    return UNITY_END();
}
