/**
 * @file    test_interlocked_slots.c
 * @brief   interlocked_slots 多槽互锁机构领域层单元测试
 *
 * 分组：
 *   B. 未初始化保护（须在 main() 中排在最前面，保证 s_slots.inited == false）
 *   A. 初始化参数校验
 *   C. 启动与停止（互锁配置下，侧刷/顶刷各自的电机槽位）
 *   D. 同刷调速（RUNNING 中切挡，仅 motor_set_speed，不触碰另一路）
 *   E. motor_phase → brush_state 映射（按槽位独立查询，不再聚合）
 *   F. 故障检测、阻断与故障码（启动检查自身故障，停止不检查故障）
 *   G. 参数校验
 *   H. 独立运行场景（无互锁配置，两个槽位可同时运行）
 *
 * @note    interlocked_slots 是 motor_executor_t 的薄封装，支持任意数量刷子槽位与
 *          可配置互锁对；接触器切换时序已下沉到 m8_motor_exec.c 内部的
 *          prepare() 回调实现，本文件不覆盖接触器时序细节（只能通过人工走
 *          查 + sim 集成场景间接验证）。
 *          setUp() 只重置 motor_executor_t，不调用 interlocked_slots_init()。
 */

#include "framework/domain/device_control/patterns/interlocked_slots.h"
#include "motor/motor_executor.h"
#include "framework/common/sw_error.h"
#include "unity.h"

#include <string.h>
#include <stddef.h>

/* 测试本地槽位命名：与项目专属命名（M8_BRUSH_SIDE/TOP）无关，仅为可读性 */
enum { T_SIDE = 0, T_TOP = 1 };

static const int                    s_motor_idx[2]     = { 0, 1 };
static const interlocked_slot_pair_t s_interlock_pair[1] = { { T_SIDE, T_TOP } };

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
static interlocked_slots_t s_slots;
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

static sw_err_t init_slots_interlocked(void)
{
    return interlocked_slots_init(&s_slots, s_hexec, s_motor_idx, 2, s_interlock_pair, 1, NULL);
}

static sw_err_t init_slots_independent(void)
{
    return interlocked_slots_init(&s_slots, s_hexec, s_motor_idx, 2, NULL, 0, NULL);
}

void setUp(void)
{
    init_exec();
    memset(&s_slots, 0, sizeof(s_slots));
    /* 不在此调用 interlocked_slots_init()，由各测试按需调用 */
}

void tearDown(void) {}

/* =========================================================================
 * B. 未初始化保护（列在 main() 最前面运行，此时 s_slots.inited == false）
 * ========================================================================= */

void test_slots_not_init_start(void)
{
    TEST_ASSERT_EQUAL(SW_ERR_NOT_INIT, interlocked_slots_start(&s_slots, T_SIDE, 1));
}

void test_slots_not_init_stop(void)
{
    TEST_ASSERT_EQUAL(SW_ERR_NOT_INIT, interlocked_slots_stop(&s_slots, T_SIDE));
}

void test_slots_not_init_stop_all(void)
{
    TEST_ASSERT_EQUAL(SW_ERR_NOT_INIT, interlocked_slots_stop_all(&s_slots));
}

void test_slots_not_init_state_idle(void)
{
    TEST_ASSERT_EQUAL(INTERLOCKED_SLOT_STATE_IDLE, interlocked_slots_state(&s_slots, T_SIDE));
}

void test_slots_not_init_fault_code_none(void)
{
    TEST_ASSERT_EQUAL(MOTOR_FAULT_NONE, interlocked_slots_fault_code(&s_slots, T_SIDE));
}

/* =========================================================================
 * A. 初始化参数校验
 * ========================================================================= */

void test_slots_init_null_exec_returns_err_param(void)
{
    TEST_ASSERT_EQUAL(SW_ERR_PARAM, interlocked_slots_init(&s_slots, NULL, s_motor_idx, 2, s_interlock_pair, 1, NULL));
}

void test_slots_init_count_exceeds_max_returns_err_param(void)
{
    int over[INTERLOCKED_SLOTS_MAX + 1] = {0};
    TEST_ASSERT_EQUAL(SW_ERR_PARAM, interlocked_slots_init(&s_slots, s_hexec, over, INTERLOCKED_SLOTS_MAX + 1, NULL, 0, NULL));
}

void test_slots_init_interlock_out_of_range_returns_err_param(void)
{
    interlocked_slot_pair_t bad = { 0, 5 };
    TEST_ASSERT_EQUAL(SW_ERR_PARAM, interlocked_slots_init(&s_slots, s_hexec, s_motor_idx, 2, &bad, 1, NULL));
}

void test_slots_init_no_interlock_ok(void)
{
    TEST_ASSERT_EQUAL(SW_OK, init_slots_independent());
}

void test_slots_init_ok(void)
{
    TEST_ASSERT_EQUAL(SW_OK, init_slots_interlocked());
}

void test_slots_state_idle_after_init(void)
{
    init_slots_interlocked();
    TEST_ASSERT_EQUAL(INTERLOCKED_SLOT_STATE_IDLE, interlocked_slots_state(&s_slots, T_SIDE));
    TEST_ASSERT_EQUAL(INTERLOCKED_SLOT_STATE_IDLE, interlocked_slots_state(&s_slots, T_TOP));
}

void test_slots_fault_code_none_after_init(void)
{
    init_slots_interlocked();
    TEST_ASSERT_EQUAL(MOTOR_FAULT_NONE, interlocked_slots_fault_code(&s_slots, T_SIDE));
}

/* =========================================================================
 * C. 启动与停止（互锁配置）
 * ========================================================================= */

void test_slots_start_side_returns_ok(void)
{
    init_slots_interlocked();
    TEST_ASSERT_EQUAL(SW_OK, interlocked_slots_start(&s_slots, T_SIDE, 1));
}

void test_slots_start_side_state_running(void)
{
    init_slots_interlocked();
    interlocked_slots_start(&s_slots, T_SIDE, 1);
    /* 仿真桩 motor_run_continuous 立即置 RUNNING */
    TEST_ASSERT_EQUAL(INTERLOCKED_SLOT_STATE_RUNNING, interlocked_slots_state(&s_slots, T_SIDE));
}

void test_slots_start_top_state_running(void)
{
    init_slots_interlocked();
    interlocked_slots_start(&s_slots, T_TOP, 1);
    TEST_ASSERT_EQUAL(INTERLOCKED_SLOT_STATE_RUNNING, interlocked_slots_state(&s_slots, T_TOP));
}

void test_slots_switch_stops_other_side(void)
{
    init_slots_interlocked();
    interlocked_slots_start(&s_slots, T_SIDE, 1);
    TEST_ASSERT_EQUAL(MOTOR_PHASE_RUNNING, s_exec.m[0].phase);

    /* 切到顶刷：互锁配置下 brush_start 内部先 motor_stop(侧刷)，仿真桩立即置 STOPPED */
    interlocked_slots_start(&s_slots, T_TOP, 1);
    TEST_ASSERT_EQUAL(MOTOR_PHASE_STOPPED, s_exec.m[0].phase);
    TEST_ASSERT_EQUAL(MOTOR_PHASE_RUNNING, s_exec.m[1].phase);
}

void test_slots_stop_returns_ok(void)
{
    init_slots_interlocked();
    interlocked_slots_start(&s_slots, T_SIDE, 1);
    TEST_ASSERT_EQUAL(SW_OK, interlocked_slots_stop(&s_slots, T_SIDE));
}

void test_slots_stop_state_idle(void)
{
    init_slots_interlocked();
    interlocked_slots_start(&s_slots, T_SIDE, 1);
    interlocked_slots_stop(&s_slots, T_SIDE);
    TEST_ASSERT_EQUAL(INTERLOCKED_SLOT_STATE_IDLE, interlocked_slots_state(&s_slots, T_SIDE));
}

void test_slots_stop_from_idle_ok(void)
{
    init_slots_interlocked();
    TEST_ASSERT_EQUAL(SW_OK, interlocked_slots_stop(&s_slots, T_SIDE));
    TEST_ASSERT_EQUAL(INTERLOCKED_SLOT_STATE_IDLE, interlocked_slots_state(&s_slots, T_SIDE));
}

void test_slots_stop_all_returns_ok(void)
{
    init_slots_interlocked();
    interlocked_slots_start(&s_slots, T_SIDE, 1);
    TEST_ASSERT_EQUAL(SW_OK, interlocked_slots_stop_all(&s_slots));
}

void test_slots_stop_all_state_idle(void)
{
    init_slots_interlocked();
    interlocked_slots_start(&s_slots, T_SIDE, 1);
    interlocked_slots_stop_all(&s_slots);
    TEST_ASSERT_EQUAL(INTERLOCKED_SLOT_STATE_IDLE, interlocked_slots_state(&s_slots, T_SIDE));
    TEST_ASSERT_EQUAL(INTERLOCKED_SLOT_STATE_IDLE, interlocked_slots_state(&s_slots, T_TOP));
}

/* =========================================================================
 * D. 同刷调速（RUNNING 中切挡，不触碰另一路）
 * ========================================================================= */

void test_slots_speed_change_same_brush_stays_running(void)
{
    init_slots_interlocked();
    interlocked_slots_start(&s_slots, T_SIDE, 1);

    TEST_ASSERT_EQUAL(SW_OK, interlocked_slots_start(&s_slots, T_SIDE, 2));
    TEST_ASSERT_EQUAL(INTERLOCKED_SLOT_STATE_RUNNING, interlocked_slots_state(&s_slots, T_SIDE));
}

void test_slots_speed_change_updates_target_freq(void)
{
    init_slots_interlocked();
    interlocked_slots_start(&s_slots, T_SIDE, 1);
    interlocked_slots_start(&s_slots, T_SIDE, 2);
    /* 仿真桩 motor_set_speed 将 target_freq 设为 spd.value = 2（挡位索引） */
    TEST_ASSERT_EQUAL_INT(2, s_exec.m[0].target_freq);
}

void test_slots_speed_change_does_not_touch_other(void)
{
    init_slots_interlocked();
    interlocked_slots_start(&s_slots, T_SIDE, 1);
    interlocked_slots_start(&s_slots, T_SIDE, 2);
    /* 顶刷电机未被触碰，仍为上电默认 STOPPED */
    TEST_ASSERT_EQUAL(MOTOR_PHASE_STOPPED, s_exec.m[1].phase);
}

/* =========================================================================
 * E. motor_phase → brush_state 映射（按槽位独立查询）
 * ========================================================================= */

void test_slots_state_side_fault_reports_fault(void)
{
    init_slots_interlocked();
    s_exec.m[0].phase = MOTOR_PHASE_FAULT;
    TEST_ASSERT_EQUAL(INTERLOCKED_SLOT_STATE_FAULT, interlocked_slots_state(&s_slots, T_SIDE));
}

void test_slots_state_top_fault_reports_fault(void)
{
    init_slots_interlocked();
    s_exec.m[1].phase = MOTOR_PHASE_FAULT;
    TEST_ASSERT_EQUAL(INTERLOCKED_SLOT_STATE_FAULT, interlocked_slots_state(&s_slots, T_TOP));
}

void test_slots_state_side_fault_does_not_affect_top(void)
{
    init_slots_interlocked();
    s_exec.m[0].phase = MOTOR_PHASE_FAULT;
    /* 故障不再聚合：侧刷故障不影响顶刷自身状态查询 */
    TEST_ASSERT_EQUAL(INTERLOCKED_SLOT_STATE_IDLE, interlocked_slots_state(&s_slots, T_TOP));
}

void test_slots_state_side_estop_reports_fault(void)
{
    init_slots_interlocked();
    s_exec.m[0].phase = MOTOR_PHASE_ESTOP;
    TEST_ASSERT_EQUAL(INTERLOCKED_SLOT_STATE_FAULT, interlocked_slots_state(&s_slots, T_SIDE));
}

void test_slots_state_side_decelerating_reports_stopping(void)
{
    init_slots_interlocked();
    s_exec.m[0].phase = MOTOR_PHASE_DECELERATING;
    TEST_ASSERT_EQUAL(INTERLOCKED_SLOT_STATE_STOPPING, interlocked_slots_state(&s_slots, T_SIDE));
}

void test_slots_state_side_waiting_start_reports_idle(void)
{
    init_slots_interlocked();
    s_exec.m[0].phase = MOTOR_PHASE_WAITING_START;
    TEST_ASSERT_EQUAL(INTERLOCKED_SLOT_STATE_IDLE, interlocked_slots_state(&s_slots, T_SIDE));
}

/* =========================================================================
 * F. 故障检测、阻断与故障码
 * ========================================================================= */

void test_slots_fault_blocks_start(void)
{
    init_slots_interlocked();
    s_exec.m[0].phase = MOTOR_PHASE_FAULT;
    TEST_ASSERT_EQUAL(SW_ERR_STATE, interlocked_slots_start(&s_slots, T_SIDE, 1));
}

void test_slots_fault_does_not_block_start_of_other(void)
{
    init_slots_interlocked();
    s_exec.m[0].phase = MOTOR_PHASE_FAULT;
    /* 侧刷故障不阻塞顶刷启动（即使二者互锁，互锁只在"对方运行中"时生效） */
    TEST_ASSERT_EQUAL(SW_OK, interlocked_slots_start(&s_slots, T_TOP, 1));
}

void test_slots_stop_does_not_check_fault(void)
{
    init_slots_interlocked();
    s_exec.m[1].phase = MOTOR_PHASE_FAULT;
    /* brush_stop 不检查故障状态，对齐 fan_stop() 的既有约定 */
    TEST_ASSERT_EQUAL(SW_OK, interlocked_slots_stop(&s_slots, T_SIDE));
}

void test_slots_stop_all_ignores_fault(void)
{
    init_slots_interlocked();
    s_exec.m[1].phase = MOTOR_PHASE_FAULT;
    TEST_ASSERT_EQUAL(SW_OK, interlocked_slots_stop_all(&s_slots));
}

void test_slots_fault_code_reflects_side(void)
{
    init_slots_interlocked();
    s_exec.m[0].phase      = MOTOR_PHASE_FAULT;
    s_exec.m[0].fault_code = MOTOR_FAULT_OVERCURRENT;
    TEST_ASSERT_EQUAL(MOTOR_FAULT_OVERCURRENT, interlocked_slots_fault_code(&s_slots, T_SIDE));
}

void test_slots_fault_code_reflects_top(void)
{
    init_slots_interlocked();
    s_exec.m[1].phase      = MOTOR_PHASE_FAULT;
    s_exec.m[1].fault_code = MOTOR_FAULT_UNDERCURRENT;
    TEST_ASSERT_EQUAL(MOTOR_FAULT_UNDERCURRENT, interlocked_slots_fault_code(&s_slots, T_TOP));
}

/* =========================================================================
 * G. 参数校验
 * ========================================================================= */

void test_slots_start_invalid_id_returns_err_param(void)
{
    init_slots_interlocked();
    TEST_ASSERT_EQUAL(SW_ERR_PARAM, interlocked_slots_start(&s_slots, 2, 1));
}

void test_slots_stop_invalid_id_returns_err_param(void)
{
    init_slots_interlocked();
    TEST_ASSERT_EQUAL(SW_ERR_PARAM, interlocked_slots_stop(&s_slots, 2));
}

/* =========================================================================
 * H. 独立运行场景（无互锁配置）
 * ========================================================================= */

void test_slots_independent_both_can_run_simultaneously(void)
{
    init_slots_independent();
    interlocked_slots_start(&s_slots, T_SIDE, 1);
    interlocked_slots_start(&s_slots, T_TOP, 1);
    TEST_ASSERT_EQUAL(INTERLOCKED_SLOT_STATE_RUNNING, interlocked_slots_state(&s_slots, T_SIDE));
    TEST_ASSERT_EQUAL(INTERLOCKED_SLOT_STATE_RUNNING, interlocked_slots_state(&s_slots, T_TOP));
}

void test_slots_independent_start_does_not_stop_other(void)
{
    init_slots_independent();
    interlocked_slots_start(&s_slots, T_SIDE, 1);
    TEST_ASSERT_EQUAL(MOTOR_PHASE_RUNNING, s_exec.m[0].phase);

    interlocked_slots_start(&s_slots, T_TOP, 1);
    /* 无互锁配置：启动顶刷不应触碰侧刷 */
    TEST_ASSERT_EQUAL(MOTOR_PHASE_RUNNING, s_exec.m[0].phase);
    TEST_ASSERT_EQUAL(MOTOR_PHASE_RUNNING, s_exec.m[1].phase);
}

/* =========================================================================
 * main
 * ========================================================================= */

int main(void)
{
    UNITY_BEGIN();

    /* B. 未初始化保护（必须最先运行） */
    RUN_TEST(test_slots_not_init_start);
    RUN_TEST(test_slots_not_init_stop);
    RUN_TEST(test_slots_not_init_stop_all);
    RUN_TEST(test_slots_not_init_state_idle);
    RUN_TEST(test_slots_not_init_fault_code_none);

    /* A. 初始化参数校验 */
    RUN_TEST(test_slots_init_null_exec_returns_err_param);
    RUN_TEST(test_slots_init_count_exceeds_max_returns_err_param);
    RUN_TEST(test_slots_init_interlock_out_of_range_returns_err_param);
    RUN_TEST(test_slots_init_no_interlock_ok);
    RUN_TEST(test_slots_init_ok);
    RUN_TEST(test_slots_state_idle_after_init);
    RUN_TEST(test_slots_fault_code_none_after_init);

    /* C. 启动与停止 */
    RUN_TEST(test_slots_start_side_returns_ok);
    RUN_TEST(test_slots_start_side_state_running);
    RUN_TEST(test_slots_start_top_state_running);
    RUN_TEST(test_slots_switch_stops_other_side);
    RUN_TEST(test_slots_stop_returns_ok);
    RUN_TEST(test_slots_stop_state_idle);
    RUN_TEST(test_slots_stop_from_idle_ok);
    RUN_TEST(test_slots_stop_all_returns_ok);
    RUN_TEST(test_slots_stop_all_state_idle);

    /* D. 同刷调速 */
    RUN_TEST(test_slots_speed_change_same_brush_stays_running);
    RUN_TEST(test_slots_speed_change_updates_target_freq);
    RUN_TEST(test_slots_speed_change_does_not_touch_other);

    /* E. motor_phase → brush_state 映射 */
    RUN_TEST(test_slots_state_side_fault_reports_fault);
    RUN_TEST(test_slots_state_top_fault_reports_fault);
    RUN_TEST(test_slots_state_side_fault_does_not_affect_top);
    RUN_TEST(test_slots_state_side_estop_reports_fault);
    RUN_TEST(test_slots_state_side_decelerating_reports_stopping);
    RUN_TEST(test_slots_state_side_waiting_start_reports_idle);

    /* F. 故障检测、阻断与故障码 */
    RUN_TEST(test_slots_fault_blocks_start);
    RUN_TEST(test_slots_fault_does_not_block_start_of_other);
    RUN_TEST(test_slots_stop_does_not_check_fault);
    RUN_TEST(test_slots_stop_all_ignores_fault);
    RUN_TEST(test_slots_fault_code_reflects_side);
    RUN_TEST(test_slots_fault_code_reflects_top);

    /* G. 参数校验 */
    RUN_TEST(test_slots_start_invalid_id_returns_err_param);
    RUN_TEST(test_slots_stop_invalid_id_returns_err_param);

    /* H. 独立运行场景 */
    RUN_TEST(test_slots_independent_both_can_run_simultaneously);
    RUN_TEST(test_slots_independent_start_does_not_stop_other);

    return UNITY_END();
}
