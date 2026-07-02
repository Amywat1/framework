/**
 * @file    test_brush.c
 * @brief   brush 刷子机构领域层单元测试
 *
 * 分组：
 *   B. 未初始化保护（须在 main() 中排在最前面，保证 brush.c s_exec == NULL）
 *   A. 初始化参数校验
 *   C. 首次启动（接触器未吸合 → tick 推进 → RUNNING）
 *   D. 停止（RUNNING → IDLE）
 *   E. 同刷再启动（接触器已在正确位置，直接启动）
 *   F. 同刷调速（RUNNING 中切挡，无接触器动作）
 *   G. 刷子切换（侧刷 → 顶刷，接触器完整切换序列）
 *   H. 故障检测与阻断
 *   I. 接触器回调验证
 *
 * @note    setUp() 只重置 motor_executor_t，不调用 brush_init()。
 *          未初始化测试（B 组）须最先运行，确保 brush.c 内 s_exec == NULL。
 *          接触器延迟均设为 0ms，tick 推进不受时间约束。
 */

#include "domain/device/mechanism/brush.h"
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
    s_cfg.watchdog_ms                    = 500;
    s_cfg.tick_ms                        = 20;
    s_cfg.motors[0].driver_index         = 0;
    s_cfg.motors[0].default_max_move_ms  = 60000;
    s_cfg.motors[0].gear_count           = 3;
    s_cfg.motors[0].gear_freq[0]         = 1500;
    s_cfg.motors[0].gear_freq[1]         = 3500;
    s_cfg.motors[0].gear_freq[2]         = 4500;

    s_ports.clock    = &s_clk;
    s_ports.drivers  = s_drv_arr;
    s_ports.encoders = NULL;
    s_ports.sensors  = &s_sensors;
    s_ports.estop    = &s_estop;

    motor_init(&s_exec, &s_cfg, &s_ports);
}

/* -------------------------------------------------------------------------
 * 接触器 mock
 * ------------------------------------------------------------------------- */
static int       s_set_on_calls  = 0;
static brush_id_t s_last_set_on_id;
static int       s_set_off_calls = 0;
static brush_id_t s_last_set_off_id;

static sw_err_t mock_set_on(void *ctx, brush_id_t id)
{
    (void)ctx;
    s_set_on_calls++;
    s_last_set_on_id = id;
    return SW_OK;
}

static sw_err_t mock_set_off(void *ctx, brush_id_t id)
{
    (void)ctx;
    s_set_off_calls++;
    s_last_set_off_id = id;
    return SW_OK;
}

static const brush_contactor_ops_t s_contactor_ops = {
    .set_on  = mock_set_on,
    .set_off = mock_set_off,
    .ctx     = NULL,
};

/* 延迟均为 0ms，tick 调用可立即推进接触器状态 */
static const brush_contactor_cfg_t s_contactor_cfg = {
    .release_ms = 0U,
    .close_ms   = 0U,
};

static void reset_mock_counters(void)
{
    s_set_on_calls    = 0;
    s_set_off_calls   = 0;
    s_last_set_on_id  = BRUSH_SIDE;
    s_last_set_off_id = BRUSH_SIDE;
}

/* -------------------------------------------------------------------------
 * 辅助：tick N 次
 * ------------------------------------------------------------------------- */
static void tick_n(int n)
{
    for (int i = 0; i < n; i++) {
        brush_tick();
    }
}

/* -------------------------------------------------------------------------
 * 辅助：从空闲启动某刷子并 tick 到 RUNNING（2 次 tick）
 * 前提：接触器尚未吸合（brush_init 后的初始状态）
 * ------------------------------------------------------------------------- */
static void start_brush_to_running(brush_id_t id, int gear)
{
    TEST_ASSERT_EQUAL(SW_OK, brush_start(id, gear));
    /* tick 1：CONTACTOR_ON → STARTING（motor_run_continuous 已调用） */
    /* tick 2：STARTING → RUNNING（ph = RUNNING） */
    tick_n(2);
    TEST_ASSERT_EQUAL(BRUSH_STATE_RUNNING, brush_state());
}

void setUp(void)
{
    init_exec();
    reset_mock_counters();
    /* 不在此调用 brush_init()，由各测试按需调用 */
}

void tearDown(void) {}

/* =========================================================================
 * B. 未初始化保护（列在 main() 最前面，此时 brush.c s_exec == NULL）
 * ========================================================================= */

void test_brush_not_init_start(void)
{
    TEST_ASSERT_EQUAL(SW_ERR_NOT_INIT, brush_start(BRUSH_SIDE, 1));
}

void test_brush_not_init_stop(void)
{
    TEST_ASSERT_EQUAL(SW_ERR_NOT_INIT, brush_stop());
}

void test_brush_not_init_fault_code_none(void)
{
    /* brush_fault_code() 有 NULL 保护，未初始化应返回 NONE */
    TEST_ASSERT_EQUAL(MOTOR_FAULT_NONE, brush_fault_code());
}

void test_brush_not_init_state_idle(void)
{
    /* brush_state() 直接返回 s_state 静态变量，初始值为 IDLE */
    TEST_ASSERT_EQUAL(BRUSH_STATE_IDLE, brush_state());
}

/* =========================================================================
 * A. 初始化参数校验
 * ========================================================================= */

void test_brush_init_null_exec(void)
{
    TEST_ASSERT_EQUAL(SW_ERR_PARAM,
        brush_init(NULL, &s_contactor_ops, &s_contactor_cfg));
}

void test_brush_init_null_ops(void)
{
    TEST_ASSERT_EQUAL(SW_ERR_PARAM,
        brush_init(&s_exec, NULL, &s_contactor_cfg));
}

void test_brush_init_null_cfg(void)
{
    TEST_ASSERT_EQUAL(SW_ERR_PARAM,
        brush_init(&s_exec, &s_contactor_ops, NULL));
}

void test_brush_init_null_set_on(void)
{
    brush_contactor_ops_t ops = s_contactor_ops;
    ops.set_on = NULL;
    TEST_ASSERT_EQUAL(SW_ERR_PARAM,
        brush_init(&s_exec, &ops, &s_contactor_cfg));
}

void test_brush_init_null_set_off(void)
{
    brush_contactor_ops_t ops = s_contactor_ops;
    ops.set_off = NULL;
    TEST_ASSERT_EQUAL(SW_ERR_PARAM,
        brush_init(&s_exec, &ops, &s_contactor_cfg));
}

void test_brush_init_ok(void)
{
    TEST_ASSERT_EQUAL(SW_OK,
        brush_init(&s_exec, &s_contactor_ops, &s_contactor_cfg));
}

void test_brush_init_state_idle(void)
{
    brush_init(&s_exec, &s_contactor_ops, &s_contactor_cfg);
    TEST_ASSERT_EQUAL(BRUSH_STATE_IDLE, brush_state());
}

void test_brush_init_contactor_not_engaged(void)
{
    brush_init(&s_exec, &s_contactor_ops, &s_contactor_cfg);
    TEST_ASSERT_FALSE(brush_contactor_engaged());
}

/* =========================================================================
 * C. 首次启动（接触器未吸合 → CONTACTOR_ON → STARTING → RUNNING）
 * ========================================================================= */

void test_brush_start_side_enters_contactor_on(void)
{
    brush_init(&s_exec, &s_contactor_ops, &s_contactor_cfg);
    TEST_ASSERT_EQUAL(SW_OK, brush_start(BRUSH_SIDE, 1));
    /* 接触器尚未吸合：brush_start → begin_contactor_on → CONTACTOR_ON */
    TEST_ASSERT_EQUAL(BRUSH_STATE_CONTACTOR_ON, brush_state());
    TEST_ASSERT_TRUE(brush_contactor_engaged());
}

void test_brush_start_side_tick_to_running(void)
{
    brush_init(&s_exec, &s_contactor_ops, &s_contactor_cfg);
    brush_start(BRUSH_SIDE, 1);
    /* tick 1：CONTACTOR_ON → STARTING（close_ms=0 立即满足） */
    brush_tick();
    TEST_ASSERT_EQUAL(BRUSH_STATE_STARTING, brush_state());
    /* tick 2：STARTING → RUNNING（仿真桩 ph=RUNNING） */
    brush_tick();
    TEST_ASSERT_EQUAL(BRUSH_STATE_RUNNING, brush_state());
}

void test_brush_start_top_tick_to_running(void)
{
    brush_init(&s_exec, &s_contactor_ops, &s_contactor_cfg);
    start_brush_to_running(BRUSH_TOP, 1);
    TEST_ASSERT_EQUAL(BRUSH_TOP, brush_selected());
}

/* =========================================================================
 * D. 停止（RUNNING → STOPPING → IDLE）
 * ========================================================================= */

void test_brush_stop_from_idle_ok(void)
{
    brush_init(&s_exec, &s_contactor_ops, &s_contactor_cfg);
    TEST_ASSERT_EQUAL(SW_OK, brush_stop());
    TEST_ASSERT_EQUAL(BRUSH_STATE_IDLE, brush_state());
}

void test_brush_stop_from_running_enters_stopping(void)
{
    brush_init(&s_exec, &s_contactor_ops, &s_contactor_cfg);
    start_brush_to_running(BRUSH_SIDE, 1);

    TEST_ASSERT_EQUAL(SW_OK, brush_stop());
    /* 仿真桩 motor_stop 立即置 STOPPED，但 brush_stop 将状态设为 STOPPING */
    TEST_ASSERT_EQUAL(BRUSH_STATE_STOPPING, brush_state());
}

void test_brush_stop_tick_to_idle(void)
{
    brush_init(&s_exec, &s_contactor_ops, &s_contactor_cfg);
    start_brush_to_running(BRUSH_SIDE, 1);

    brush_stop();
    /* tick：ph=STOPPED，start_pending=false → IDLE */
    brush_tick();
    TEST_ASSERT_EQUAL(BRUSH_STATE_IDLE, brush_state());
    /* 停止后接触器保持原位（不断电） */
    TEST_ASSERT_TRUE(brush_contactor_engaged());
    TEST_ASSERT_EQUAL(BRUSH_SIDE, brush_selected());
}

void test_brush_stop_from_contactor_on_then_idle(void)
{
    brush_init(&s_exec, &s_contactor_ops, &s_contactor_cfg);
    /* 触发 brush_start → CONTACTOR_ON，然后立即 stop */
    brush_start(BRUSH_SIDE, 1);
    TEST_ASSERT_EQUAL(BRUSH_STATE_CONTACTOR_ON, brush_state());

    /* stop：s_start_pending=false，motor_stop，→ STOPPING */
    TEST_ASSERT_EQUAL(SW_OK, brush_stop());
    TEST_ASSERT_EQUAL(BRUSH_STATE_STOPPING, brush_state());

    /* tick：ph=STOPPED，→ IDLE */
    brush_tick();
    TEST_ASSERT_EQUAL(BRUSH_STATE_IDLE, brush_state());
}

/* =========================================================================
 * E. 同刷再启动（接触器已在正确位置，直接 STARTING → RUNNING）
 * ========================================================================= */

void test_brush_restart_same_brush_directly_starting(void)
{
    brush_init(&s_exec, &s_contactor_ops, &s_contactor_cfg);
    /* 首次启动到 RUNNING 再停止，接触器保持在 BRUSH_SIDE */
    start_brush_to_running(BRUSH_SIDE, 1);
    brush_stop();
    brush_tick();
    TEST_ASSERT_EQUAL(BRUSH_STATE_IDLE, brush_state());

    reset_mock_counters();

    /* 再次启动同一刷子，接触器无需切换 */
    TEST_ASSERT_EQUAL(SW_OK, brush_start(BRUSH_SIDE, 1));
    /* 不经过 CONTACTOR_ON，直接到 STARTING */
    TEST_ASSERT_EQUAL(BRUSH_STATE_STARTING, brush_state());

    /* 无额外的 set_on / set_off 调用 */
    TEST_ASSERT_EQUAL(0, s_set_on_calls);
    TEST_ASSERT_EQUAL(0, s_set_off_calls);
}

void test_brush_restart_same_brush_tick_to_running(void)
{
    brush_init(&s_exec, &s_contactor_ops, &s_contactor_cfg);
    start_brush_to_running(BRUSH_SIDE, 1);
    brush_stop();
    brush_tick();

    brush_start(BRUSH_SIDE, 1);
    brush_tick();  /* STARTING → RUNNING */
    TEST_ASSERT_EQUAL(BRUSH_STATE_RUNNING, brush_state());
}

/* =========================================================================
 * F. 同刷调速（RUNNING 中切挡，不经过接触器）
 * ========================================================================= */

void test_brush_speed_change_same_brush_stays_running(void)
{
    brush_init(&s_exec, &s_contactor_ops, &s_contactor_cfg);
    start_brush_to_running(BRUSH_SIDE, 1);

    reset_mock_counters();

    /* 同刷子不同挡位：仅调速，状态保持 RUNNING */
    TEST_ASSERT_EQUAL(SW_OK, brush_start(BRUSH_SIDE, 2));
    TEST_ASSERT_EQUAL(BRUSH_STATE_RUNNING, brush_state());

    /* 无接触器动作 */
    TEST_ASSERT_EQUAL(0, s_set_on_calls);
    TEST_ASSERT_EQUAL(0, s_set_off_calls);
}

void test_brush_speed_change_updates_target_freq(void)
{
    brush_init(&s_exec, &s_contactor_ops, &s_contactor_cfg);
    start_brush_to_running(BRUSH_SIDE, 1);

    brush_start(BRUSH_SIDE, 2);
    /* 仿真桩 motor_set_speed 将 target_freq 设为 spd.value = 2（挡位索引） */
    TEST_ASSERT_EQUAL_INT(2, s_exec.m[0].target_freq);
}

/* =========================================================================
 * G. 刷子切换（侧刷 → 顶刷：STOPPING→CONTACTOR_OFF→CONTACTOR_ON→STARTING→RUNNING）
 * ========================================================================= */

void test_brush_switch_from_running_enters_stopping(void)
{
    brush_init(&s_exec, &s_contactor_ops, &s_contactor_cfg);
    start_brush_to_running(BRUSH_SIDE, 1);

    reset_mock_counters();

    /* 切换到顶刷：先停变频器 */
    TEST_ASSERT_EQUAL(SW_OK, brush_start(BRUSH_TOP, 1));
    TEST_ASSERT_EQUAL(BRUSH_STATE_STOPPING, brush_state());
}

void test_brush_switch_full_sequence(void)
{
    brush_init(&s_exec, &s_contactor_ops, &s_contactor_cfg);
    start_brush_to_running(BRUSH_SIDE, 1);

    reset_mock_counters();
    brush_start(BRUSH_TOP, 1);

    /* tick 1：STOPPING → CONTACTOR_OFF（begin_contactor_off，set_off 调用一次） */
    brush_tick();
    TEST_ASSERT_EQUAL(BRUSH_STATE_CONTACTOR_OFF, brush_state());
    TEST_ASSERT_EQUAL(1, s_set_off_calls);
    TEST_ASSERT_EQUAL(BRUSH_SIDE, s_last_set_off_id);

    /* tick 2：CONTACTOR_OFF → CONTACTOR_ON（begin_contactor_on，set_on 调用一次） */
    brush_tick();
    TEST_ASSERT_EQUAL(BRUSH_STATE_CONTACTOR_ON, brush_state());
    TEST_ASSERT_EQUAL(1, s_set_on_calls);
    TEST_ASSERT_EQUAL(BRUSH_TOP, s_last_set_on_id);

    /* tick 3：CONTACTOR_ON → STARTING */
    brush_tick();
    TEST_ASSERT_EQUAL(BRUSH_STATE_STARTING, brush_state());

    /* tick 4：STARTING → RUNNING */
    brush_tick();
    TEST_ASSERT_EQUAL(BRUSH_STATE_RUNNING, brush_state());
    TEST_ASSERT_EQUAL(BRUSH_TOP, brush_selected());
}

void test_brush_switch_from_idle_with_wrong_contactor(void)
{
    /* 从 IDLE 状态、接触器已在 SIDE 位置，启动 TOP → 先断开再吸合 */
    brush_init(&s_exec, &s_contactor_ops, &s_contactor_cfg);
    /* 首次到 RUNNING 再停到 IDLE，接触器留在 SIDE */
    start_brush_to_running(BRUSH_SIDE, 1);
    brush_stop();
    brush_tick();
    TEST_ASSERT_EQUAL(BRUSH_STATE_IDLE, brush_state());

    reset_mock_counters();

    /* 从 IDLE 请求 TOP */
    brush_start(BRUSH_TOP, 1);
    /* IDLE + engaged + wrong contactor → CONTACTOR_OFF */
    TEST_ASSERT_EQUAL(BRUSH_STATE_CONTACTOR_OFF, brush_state());
    TEST_ASSERT_EQUAL(1, s_set_off_calls);

    /* tick 1：CONTACTOR_OFF → CONTACTOR_ON */
    brush_tick();
    TEST_ASSERT_EQUAL(BRUSH_STATE_CONTACTOR_ON, brush_state());
    TEST_ASSERT_EQUAL(1, s_set_on_calls);
    TEST_ASSERT_EQUAL(BRUSH_TOP, s_last_set_on_id);

    /* tick 2：CONTACTOR_ON → STARTING */
    brush_tick();
    TEST_ASSERT_EQUAL(BRUSH_STATE_STARTING, brush_state());

    /* tick 3：STARTING → RUNNING */
    brush_tick();
    TEST_ASSERT_EQUAL(BRUSH_STATE_RUNNING, brush_state());
    TEST_ASSERT_EQUAL(BRUSH_TOP, brush_selected());
}

/* =========================================================================
 * H. 故障检测与阻断
 * ========================================================================= */

void test_brush_fault_detected_by_tick(void)
{
    brush_init(&s_exec, &s_contactor_ops, &s_contactor_cfg);
    start_brush_to_running(BRUSH_SIDE, 1);

    /* 直接注入故障状态 */
    s_exec.m[0].phase = MOTOR_PHASE_FAULT;
    brush_tick();
    TEST_ASSERT_EQUAL(BRUSH_STATE_FAULT, brush_state());
}

void test_brush_estop_detected_by_tick(void)
{
    brush_init(&s_exec, &s_contactor_ops, &s_contactor_cfg);
    start_brush_to_running(BRUSH_SIDE, 1);

    s_exec.m[0].phase = MOTOR_PHASE_ESTOP;
    brush_tick();
    TEST_ASSERT_EQUAL(BRUSH_STATE_FAULT, brush_state());
}

void test_brush_fault_blocks_start(void)
{
    brush_init(&s_exec, &s_contactor_ops, &s_contactor_cfg);
    start_brush_to_running(BRUSH_SIDE, 1);
    s_exec.m[0].phase = MOTOR_PHASE_FAULT;
    brush_tick();

    TEST_ASSERT_EQUAL(SW_ERR_STATE, brush_start(BRUSH_SIDE, 1));
}

void test_brush_fault_blocks_stop(void)
{
    brush_init(&s_exec, &s_contactor_ops, &s_contactor_cfg);
    start_brush_to_running(BRUSH_SIDE, 1);
    s_exec.m[0].phase = MOTOR_PHASE_FAULT;
    brush_tick();

    TEST_ASSERT_EQUAL(SW_ERR_STATE, brush_stop());
}

void test_brush_fault_code_reflected(void)
{
    brush_init(&s_exec, &s_contactor_ops, &s_contactor_cfg);
    s_exec.m[0].phase      = MOTOR_PHASE_FAULT;
    s_exec.m[0].fault_code = MOTOR_FAULT_OVERCURRENT;
    brush_tick();

    TEST_ASSERT_EQUAL(MOTOR_FAULT_OVERCURRENT, brush_fault_code());
}

/* =========================================================================
 * I. 接触器回调验证
 * ========================================================================= */

void test_brush_first_start_calls_set_on(void)
{
    brush_init(&s_exec, &s_contactor_ops, &s_contactor_cfg);
    reset_mock_counters();

    brush_start(BRUSH_SIDE, 1);
    /* begin_contactor_on 在 brush_start 内被调用 */
    TEST_ASSERT_EQUAL(1, s_set_on_calls);
    TEST_ASSERT_EQUAL(BRUSH_SIDE, s_last_set_on_id);
    TEST_ASSERT_EQUAL(0, s_set_off_calls);
}

void test_brush_start_invalid_id_returns_err_param(void)
{
    brush_init(&s_exec, &s_contactor_ops, &s_contactor_cfg);
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
    RUN_TEST(test_brush_not_init_fault_code_none);
    RUN_TEST(test_brush_not_init_state_idle);

    /* A. 初始化参数校验 */
    RUN_TEST(test_brush_init_null_exec);
    RUN_TEST(test_brush_init_null_ops);
    RUN_TEST(test_brush_init_null_cfg);
    RUN_TEST(test_brush_init_null_set_on);
    RUN_TEST(test_brush_init_null_set_off);
    RUN_TEST(test_brush_init_ok);
    RUN_TEST(test_brush_init_state_idle);
    RUN_TEST(test_brush_init_contactor_not_engaged);

    /* C. 首次启动 */
    RUN_TEST(test_brush_start_side_enters_contactor_on);
    RUN_TEST(test_brush_start_side_tick_to_running);
    RUN_TEST(test_brush_start_top_tick_to_running);

    /* D. 停止 */
    RUN_TEST(test_brush_stop_from_idle_ok);
    RUN_TEST(test_brush_stop_from_running_enters_stopping);
    RUN_TEST(test_brush_stop_tick_to_idle);
    RUN_TEST(test_brush_stop_from_contactor_on_then_idle);

    /* E. 同刷再启动 */
    RUN_TEST(test_brush_restart_same_brush_directly_starting);
    RUN_TEST(test_brush_restart_same_brush_tick_to_running);

    /* F. 同刷调速 */
    RUN_TEST(test_brush_speed_change_same_brush_stays_running);
    RUN_TEST(test_brush_speed_change_updates_target_freq);

    /* G. 刷子切换 */
    RUN_TEST(test_brush_switch_from_running_enters_stopping);
    RUN_TEST(test_brush_switch_full_sequence);
    RUN_TEST(test_brush_switch_from_idle_with_wrong_contactor);

    /* H. 故障检测与阻断 */
    RUN_TEST(test_brush_fault_detected_by_tick);
    RUN_TEST(test_brush_estop_detected_by_tick);
    RUN_TEST(test_brush_fault_blocks_start);
    RUN_TEST(test_brush_fault_blocks_stop);
    RUN_TEST(test_brush_fault_code_reflected);

    /* I. 接触器回调验证 */
    RUN_TEST(test_brush_first_start_calls_set_on);
    RUN_TEST(test_brush_start_invalid_id_returns_err_param);

    return UNITY_END();
}
