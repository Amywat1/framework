/**
 * @file    test_brush.c
 * @brief   brush 刷子机构领域层单元测试
 *
 * 分组：
 *   B. 未初始化保护（须在 main() 中排在最前面，保证 brush.c s_exec == NULL）
 *   A. 初始化参数校验
 *   C. 启动与停止（侧刷/顶刷各自的电机槽位）
 *   D. 同刷调速（RUNNING 中切挡，仅 motor_set_speed，不触碰另一路）
 *   E. motor_phase → brush_state 映射（任一槽位）
 *   F. 故障检测、阻断与故障码
 *   G. 参数校验
 *
 * @note    brush.c 现在是 motor_executor_t 的薄封装：侧刷/顶刷各占一个电机
 *          槽位，接触器切换时序已下沉到 m8_motor_exec.c 内部的 prepare() 回调
 *          实现，不再是独立可单测的模块，本文件不覆盖接触器时序细节（只能
 *          通过人工走查 + sim 集成场景间接验证）。
 *          setUp() 只重置 motor_executor_t，不调用 brush_init()。
 */

#include "framework/domain/device_control/mechanism/brush.h"
#include "motor/motor_executor.h"
#include "framework/common/sw_error.h"
#include "unity.h"

#include <string.h>
#include <stddef.h>

/* -------------------------------------------------------------------------
 * 最小 motor_executor_t 初始化辅助（2 个电机槽位：侧刷=0，顶刷=1）
 * ------------------------------------------------------------------------- */
static motor_executor_t  s_exec;
static motor_driver_t    s_drv_side;
static motor_driver_t    s_drv_top;
static motor_driver_t   *s_drv_arr[2] = {&s_drv_side, &s_drv_top};
static motor_clock_t     s_clk;
static motor_sensors_t   s_sensors;
static motor_estop_t     s_estop;
static motor_ports_t     s_ports;
static motor_config_t    s_cfg;
static hal_motor_exec_t *const s_hexec = (hal_motor_exec_t *)&s_exec;

static void init_exec(void)
{
    int i;

    memset(&s_cfg,      0, sizeof(s_cfg));
    memset(&s_drv_side, 0, sizeof(s_drv_side));
    memset(&s_drv_top,  0, sizeof(s_drv_top));
    memset(&s_clk,      0, sizeof(s_clk));
    memset(&s_sensors,  0, sizeof(s_sensors));
    memset(&s_estop,    0, sizeof(s_estop));
    memset(&s_ports,    0, sizeof(s_ports));

    s_cfg.motor_count  = 2;
    s_cfg.driver_count = 2;
    s_cfg.watchdog_ms  = 200;
    s_cfg.tick_ms      = 20;
    for (i = 0; i < 2; i++) {
        s_cfg.motors[i].driver_index        = i;
        s_cfg.motors[i].default_max_move_ms = 300000;
        s_cfg.motors[i].gear_count          = 3;
        s_cfg.motors[i].gear_freq[0]        = 2000;
        s_cfg.motors[i].gear_freq[1]        = 3500;
        s_cfg.motors[i].gear_freq[2]        = 4500;
    }

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
    /* 不在此调用 brush_init()，由各测试按需调用 */
}

void tearDown(void) {}

/* =========================================================================
 * B. 未初始化保护（列在 main() 最前面运行，此时 brush.c s_exec == NULL）
 * ========================================================================= */

void test_brush_not_init_start(void)
{
    TEST_ASSERT_EQUAL(SW_ERR_NOT_INIT, brush_start(BRUSH_SIDE, 1));
}

void test_brush_not_init_stop(void)
{
    TEST_ASSERT_EQUAL(SW_ERR_NOT_INIT, brush_stop());
}

void test_brush_not_init_state_idle(void)
{
    TEST_ASSERT_EQUAL(BRUSH_STATE_IDLE, brush_state());
}

void test_brush_not_init_fault_code_none(void)
{
    TEST_ASSERT_EQUAL(MOTOR_FAULT_NONE, brush_fault_code());
}

void test_brush_not_init_selected_default_side(void)
{
    TEST_ASSERT_EQUAL(BRUSH_SIDE, brush_selected());
}

/* =========================================================================
 * A. 初始化参数校验
 * ========================================================================= */

void test_brush_init_null_returns_err_param(void)
{
    TEST_ASSERT_EQUAL(SW_ERR_PARAM, brush_init(NULL, 0, 1));
}

void test_brush_init_ok(void)
{
    TEST_ASSERT_EQUAL(SW_OK, brush_init(s_hexec, 0, 1));
}

void test_brush_state_idle_after_init(void)
{
    brush_init(s_hexec, 0, 1);
    TEST_ASSERT_EQUAL(BRUSH_STATE_IDLE, brush_state());
}

void test_brush_fault_code_none_after_init(void)
{
    brush_init(s_hexec, 0, 1);
    TEST_ASSERT_EQUAL(MOTOR_FAULT_NONE, brush_fault_code());
}

/* =========================================================================
 * C. 启动与停止
 * ========================================================================= */

void test_brush_start_side_returns_ok(void)
{
    brush_init(s_hexec, 0, 1);
    TEST_ASSERT_EQUAL(SW_OK, brush_start(BRUSH_SIDE, 1));
}

void test_brush_start_side_state_running(void)
{
    brush_init(s_hexec, 0, 1);
    brush_start(BRUSH_SIDE, 1);
    /* 仿真桩 motor_run_continuous 立即置 RUNNING */
    TEST_ASSERT_EQUAL(BRUSH_STATE_RUNNING, brush_state());
}

void test_brush_start_side_selected_side(void)
{
    brush_init(s_hexec, 0, 1);
    brush_start(BRUSH_SIDE, 1);
    TEST_ASSERT_EQUAL(BRUSH_SIDE, brush_selected());
}

void test_brush_start_top_state_running(void)
{
    brush_init(s_hexec, 0, 1);
    brush_start(BRUSH_TOP, 1);
    TEST_ASSERT_EQUAL(BRUSH_STATE_RUNNING, brush_state());
    TEST_ASSERT_EQUAL(BRUSH_TOP, brush_selected());
}

void test_brush_switch_stops_other_side(void)
{
    brush_init(s_hexec, 0, 1);
    brush_start(BRUSH_SIDE, 1);
    TEST_ASSERT_EQUAL(MOTOR_PHASE_RUNNING, s_exec.m[0].phase);

    /* 切到顶刷：brush_start 内部先 motor_stop(侧刷)，仿真桩立即置 STOPPED */
    brush_start(BRUSH_TOP, 1);
    TEST_ASSERT_EQUAL(MOTOR_PHASE_STOPPED, s_exec.m[0].phase);
    TEST_ASSERT_EQUAL(MOTOR_PHASE_RUNNING, s_exec.m[1].phase);
    TEST_ASSERT_EQUAL(BRUSH_TOP, brush_selected());
}

void test_brush_stop_returns_ok(void)
{
    brush_init(s_hexec, 0, 1);
    brush_start(BRUSH_SIDE, 1);
    TEST_ASSERT_EQUAL(SW_OK, brush_stop());
}

void test_brush_stop_state_idle(void)
{
    brush_init(s_hexec, 0, 1);
    brush_start(BRUSH_SIDE, 1);
    brush_stop();
    TEST_ASSERT_EQUAL(BRUSH_STATE_IDLE, brush_state());
}

void test_brush_stop_from_idle_ok(void)
{
    brush_init(s_hexec, 0, 1);
    TEST_ASSERT_EQUAL(SW_OK, brush_stop());
    TEST_ASSERT_EQUAL(BRUSH_STATE_IDLE, brush_state());
}

/* =========================================================================
 * D. 同刷调速（RUNNING 中切挡，不触碰另一路）
 * ========================================================================= */

void test_brush_speed_change_same_brush_stays_running(void)
{
    brush_init(s_hexec, 0, 1);
    brush_start(BRUSH_SIDE, 1);

    TEST_ASSERT_EQUAL(SW_OK, brush_start(BRUSH_SIDE, 2));
    TEST_ASSERT_EQUAL(BRUSH_STATE_RUNNING, brush_state());
}

void test_brush_speed_change_updates_target_freq(void)
{
    brush_init(s_hexec, 0, 1);
    brush_start(BRUSH_SIDE, 1);
    brush_start(BRUSH_SIDE, 2);
    /* 仿真桩 motor_set_speed 将 target_freq 设为 spd.value = 2（挡位索引） */
    TEST_ASSERT_EQUAL_INT(2, s_exec.m[0].target_freq);
}

void test_brush_speed_change_does_not_touch_other(void)
{
    brush_init(s_hexec, 0, 1);
    brush_start(BRUSH_SIDE, 1);
    brush_start(BRUSH_SIDE, 2);
    /* 顶刷电机未被触碰，仍为上电默认 STOPPED */
    TEST_ASSERT_EQUAL(MOTOR_PHASE_STOPPED, s_exec.m[1].phase);
}

/* =========================================================================
 * E. motor_phase → brush_state 映射
 * ========================================================================= */

void test_brush_state_side_fault_reports_fault(void)
{
    brush_init(s_hexec, 0, 1);
    s_exec.m[0].phase = MOTOR_PHASE_FAULT;
    TEST_ASSERT_EQUAL(BRUSH_STATE_FAULT, brush_state());
}

void test_brush_state_top_fault_reports_fault(void)
{
    brush_init(s_hexec, 0, 1);
    s_exec.m[1].phase = MOTOR_PHASE_FAULT;
    TEST_ASSERT_EQUAL(BRUSH_STATE_FAULT, brush_state());
}

void test_brush_state_side_estop_reports_fault(void)
{
    brush_init(s_hexec, 0, 1);
    s_exec.m[0].phase = MOTOR_PHASE_ESTOP;
    TEST_ASSERT_EQUAL(BRUSH_STATE_FAULT, brush_state());
}

void test_brush_state_side_decelerating_reports_stopping(void)
{
    brush_init(s_hexec, 0, 1);
    s_exec.m[0].phase = MOTOR_PHASE_DECELERATING;
    TEST_ASSERT_EQUAL(BRUSH_STATE_STOPPING, brush_state());
}

void test_brush_state_side_waiting_start_reports_idle(void)
{
    brush_init(s_hexec, 0, 1);
    s_exec.m[0].phase = MOTOR_PHASE_WAITING_START;
    TEST_ASSERT_EQUAL(BRUSH_STATE_IDLE, brush_state());
}

/* =========================================================================
 * F. 故障检测、阻断与故障码
 * ========================================================================= */

void test_brush_fault_blocks_start(void)
{
    brush_init(s_hexec, 0, 1);
    s_exec.m[0].phase = MOTOR_PHASE_FAULT;
    TEST_ASSERT_EQUAL(SW_ERR_STATE, brush_start(BRUSH_SIDE, 1));
}

void test_brush_fault_blocks_stop(void)
{
    brush_init(s_hexec, 0, 1);
    s_exec.m[1].phase = MOTOR_PHASE_FAULT;
    TEST_ASSERT_EQUAL(SW_ERR_STATE, brush_stop());
}

void test_brush_fault_code_reflects_side(void)
{
    brush_init(s_hexec, 0, 1);
    s_exec.m[0].phase      = MOTOR_PHASE_FAULT;
    s_exec.m[0].fault_code = MOTOR_FAULT_OVERCURRENT;
    TEST_ASSERT_EQUAL(MOTOR_FAULT_OVERCURRENT, brush_fault_code());
}

void test_brush_fault_code_reflects_top(void)
{
    brush_init(s_hexec, 0, 1);
    s_exec.m[1].phase      = MOTOR_PHASE_FAULT;
    s_exec.m[1].fault_code = MOTOR_FAULT_UNDERCURRENT;
    TEST_ASSERT_EQUAL(MOTOR_FAULT_UNDERCURRENT, brush_fault_code());
}

/* =========================================================================
 * G. 参数校验
 * ========================================================================= */

void test_brush_start_invalid_id_returns_err_param(void)
{
    brush_init(s_hexec, 0, 1);
    TEST_ASSERT_EQUAL(SW_ERR_PARAM, brush_start(BRUSH_ID_MAX, 1));
}

/* =========================================================================
 * main
 * ========================================================================= */

int main(void)
{
    UNITY_BEGIN();

    /* B. 未初始化保护（必须最先运行） */
    RUN_TEST(test_brush_not_init_start);
    RUN_TEST(test_brush_not_init_stop);
    RUN_TEST(test_brush_not_init_state_idle);
    RUN_TEST(test_brush_not_init_fault_code_none);
    RUN_TEST(test_brush_not_init_selected_default_side);

    /* A. 初始化参数校验 */
    RUN_TEST(test_brush_init_null_returns_err_param);
    RUN_TEST(test_brush_init_ok);
    RUN_TEST(test_brush_state_idle_after_init);
    RUN_TEST(test_brush_fault_code_none_after_init);

    /* C. 启动与停止 */
    RUN_TEST(test_brush_start_side_returns_ok);
    RUN_TEST(test_brush_start_side_state_running);
    RUN_TEST(test_brush_start_side_selected_side);
    RUN_TEST(test_brush_start_top_state_running);
    RUN_TEST(test_brush_switch_stops_other_side);
    RUN_TEST(test_brush_stop_returns_ok);
    RUN_TEST(test_brush_stop_state_idle);
    RUN_TEST(test_brush_stop_from_idle_ok);

    /* D. 同刷调速 */
    RUN_TEST(test_brush_speed_change_same_brush_stays_running);
    RUN_TEST(test_brush_speed_change_updates_target_freq);
    RUN_TEST(test_brush_speed_change_does_not_touch_other);

    /* E. motor_phase → brush_state 映射 */
    RUN_TEST(test_brush_state_side_fault_reports_fault);
    RUN_TEST(test_brush_state_top_fault_reports_fault);
    RUN_TEST(test_brush_state_side_estop_reports_fault);
    RUN_TEST(test_brush_state_side_decelerating_reports_stopping);
    RUN_TEST(test_brush_state_side_waiting_start_reports_idle);

    /* F. 故障检测、阻断与故障码 */
    RUN_TEST(test_brush_fault_blocks_start);
    RUN_TEST(test_brush_fault_blocks_stop);
    RUN_TEST(test_brush_fault_code_reflects_side);
    RUN_TEST(test_brush_fault_code_reflects_top);

    /* G. 参数校验 */
    RUN_TEST(test_brush_start_invalid_id_returns_err_param);

    return UNITY_END();
}
