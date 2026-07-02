/**
 * @file    test_fan.c
 * @brief   fan 风机机构领域层单元测试
 *
 * 分组：
 *   B. 未初始化保护（须在 main() 中排在最前面，保证 fan.c s_ops.set_start == NULL）
 *   A. 初始化参数校验
 *   C. 启动与停止（IDLE ↔ RUNNING）
 *   D. 报警检测 → FAULT（tick 触发）
 *   E. 故障态命令拒绝
 *   F. 复位流程（FAULT → RESETTING）
 *   G. 复位脉冲超时后恢复 IDLE（报警已消）
 *   H. 复位脉冲超时后继续 FAULT（报警未消）
 *
 * @note    setUp() 清零 mock 状态，不调用 fan_init()。
 *          未初始化测试（B 组）须最先运行，确保 fan.c 内 s_ops.set_start == NULL。
 *
 *          G/H 组需等待真实时间超过 reset_pulse_ms；配置 reset_pulse_ms = 10ms，
 *          并在 fan_reset() 之后 usleep(20000)（20ms）再调 fan_tick()。
 */

#include "domain/device/mechanism/fan.h"
#include "common/sw_error.h"
#include "unity.h"

#include <string.h>
#include <stddef.h>
#include <unistd.h>

/* -------------------------------------------------------------------------
 * mock IO 回调
 * ------------------------------------------------------------------------- */
static bool s_start_on        = false;
static bool s_reset_on        = false;
static bool s_alarm           = false;
static int  s_set_start_calls = 0;
static int  s_set_reset_calls = 0;

static sw_err_t mock_set_start(void *ctx, bool on)
{
    (void)ctx;
    s_start_on = on;
    s_set_start_calls++;
    return SW_OK;
}

static sw_err_t mock_set_reset(void *ctx, bool on)
{
    (void)ctx;
    s_reset_on = on;
    s_set_reset_calls++;
    return SW_OK;
}

static bool mock_read_alarm(void *ctx)
{
    (void)ctx;
    return s_alarm;
}

static const fan_io_ops_t s_ops = {
    .set_start  = mock_set_start,
    .set_reset  = mock_set_reset,
    .read_alarm = mock_read_alarm,
    .ctx        = NULL,
};

/* reset_pulse_ms = 10 使脉冲超时测试只需 ~20ms 真实等待 */
static const fan_cfg_t s_cfg = {
    .reset_pulse_ms = 10U,
};

static void reset_mock(void)
{
    s_start_on        = false;
    s_reset_on        = false;
    s_alarm           = false;
    s_set_start_calls = 0;
    s_set_reset_calls = 0;
}

void setUp(void)
{
    reset_mock();
    /* 不在此调用 fan_init()，由各测试按需调用 */
}

void tearDown(void) {}

/* =========================================================================
 * B. 未初始化保护（列在 main() 最前面，此时 fan.c 内回调指针均为 NULL）
 * ========================================================================= */

void test_fan_not_init_start(void)
{
    TEST_ASSERT_EQUAL(SW_ERR_NOT_INIT, fan_start());
}

void test_fan_not_init_stop(void)
{
    TEST_ASSERT_EQUAL(SW_ERR_NOT_INIT, fan_stop());
}

void test_fan_not_init_reset(void)
{
    TEST_ASSERT_EQUAL(SW_ERR_NOT_INIT, fan_reset());
}

void test_fan_not_init_state_idle(void)
{
    /* 未初始化时 fan_state() 应安全返回 IDLE */
    TEST_ASSERT_EQUAL(FAN_STATE_IDLE, fan_state());
}

void test_fan_not_init_tick_safe(void)
{
    /* 未初始化时 fan_tick() 不崩溃，且 mock 回调不被调用 */
    fan_tick();
    TEST_ASSERT_EQUAL(0, s_set_start_calls);
}

/* =========================================================================
 * A. 初始化参数校验
 * ========================================================================= */

void test_fan_init_null_ops(void)
{
    TEST_ASSERT_EQUAL(SW_ERR_PARAM, fan_init(NULL, &s_cfg));
}

void test_fan_init_null_cfg(void)
{
    TEST_ASSERT_EQUAL(SW_ERR_PARAM, fan_init(&s_ops, NULL));
}

void test_fan_init_null_set_start(void)
{
    fan_io_ops_t ops = s_ops;
    ops.set_start = NULL;
    TEST_ASSERT_EQUAL(SW_ERR_PARAM, fan_init(&ops, &s_cfg));
}

void test_fan_init_null_set_reset(void)
{
    fan_io_ops_t ops = s_ops;
    ops.set_reset = NULL;
    TEST_ASSERT_EQUAL(SW_ERR_PARAM, fan_init(&ops, &s_cfg));
}

void test_fan_init_null_read_alarm(void)
{
    fan_io_ops_t ops = s_ops;
    ops.read_alarm = NULL;
    TEST_ASSERT_EQUAL(SW_ERR_PARAM, fan_init(&ops, &s_cfg));
}

void test_fan_init_zero_reset_pulse(void)
{
    fan_cfg_t cfg = { .reset_pulse_ms = 0U };
    TEST_ASSERT_EQUAL(SW_ERR_PARAM, fan_init(&s_ops, &cfg));
}

void test_fan_init_ok(void)
{
    TEST_ASSERT_EQUAL(SW_OK, fan_init(&s_ops, &s_cfg));
}

void test_fan_state_idle_after_init(void)
{
    fan_init(&s_ops, &s_cfg);
    TEST_ASSERT_EQUAL(FAN_STATE_IDLE, fan_state());
}

/* =========================================================================
 * C. 启动与停止（IDLE ↔ RUNNING）
 * ========================================================================= */

void test_fan_start_returns_ok(void)
{
    fan_init(&s_ops, &s_cfg);
    TEST_ASSERT_EQUAL(SW_OK, fan_start());
}

void test_fan_start_state_running(void)
{
    fan_init(&s_ops, &s_cfg);
    fan_start();
    TEST_ASSERT_EQUAL(FAN_STATE_RUNNING, fan_state());
}

void test_fan_start_activates_do(void)
{
    fan_init(&s_ops, &s_cfg);
    fan_start();
    TEST_ASSERT_TRUE(s_start_on);
}

void test_fan_stop_returns_ok(void)
{
    fan_init(&s_ops, &s_cfg);
    fan_start();
    TEST_ASSERT_EQUAL(SW_OK, fan_stop());
}

void test_fan_stop_state_idle(void)
{
    fan_init(&s_ops, &s_cfg);
    fan_start();
    fan_stop();
    TEST_ASSERT_EQUAL(FAN_STATE_IDLE, fan_state());
}

void test_fan_stop_deactivates_do(void)
{
    fan_init(&s_ops, &s_cfg);
    fan_start();
    fan_stop();
    TEST_ASSERT_FALSE(s_start_on);
}

void test_fan_stop_from_idle_ok(void)
{
    fan_init(&s_ops, &s_cfg);
    /* IDLE 态停止不应报错 */
    TEST_ASSERT_EQUAL(SW_OK, fan_stop());
    TEST_ASSERT_EQUAL(FAN_STATE_IDLE, fan_state());
}

/* =========================================================================
 * D. 报警检测 → FAULT（tick 触发）
 * ========================================================================= */

void test_fan_tick_alarm_in_running_goes_fault(void)
{
    fan_init(&s_ops, &s_cfg);
    fan_start();
    s_alarm = true;
    fan_tick();
    TEST_ASSERT_EQUAL(FAN_STATE_FAULT, fan_state());
}

void test_fan_tick_alarm_stops_start_do(void)
{
    fan_init(&s_ops, &s_cfg);
    fan_start();
    s_alarm = true;
    fan_tick();
    /* 进入 FAULT 时 set_start(false) 应被调用 */
    TEST_ASSERT_FALSE(s_start_on);
}

void test_fan_tick_alarm_in_idle_goes_fault(void)
{
    fan_init(&s_ops, &s_cfg);
    /* IDLE 态 tick 同样检测报警 */
    s_alarm = true;
    fan_tick();
    TEST_ASSERT_EQUAL(FAN_STATE_FAULT, fan_state());
}

void test_fan_tick_no_alarm_stays_running(void)
{
    fan_init(&s_ops, &s_cfg);
    fan_start();
    s_alarm = false;
    fan_tick();
    TEST_ASSERT_EQUAL(FAN_STATE_RUNNING, fan_state());
}

/* =========================================================================
 * E. 故障态命令拒绝
 * ========================================================================= */

void test_fan_fault_blocks_start(void)
{
    fan_init(&s_ops, &s_cfg);
    s_alarm = true;
    fan_tick();   /* → FAULT */
    TEST_ASSERT_EQUAL(SW_ERR_STATE, fan_start());
}

void test_fan_fault_blocks_stop(void)
{
    fan_init(&s_ops, &s_cfg);
    s_alarm = true;
    fan_tick();   /* → FAULT */
    TEST_ASSERT_EQUAL(SW_ERR_STATE, fan_stop());
}

/* =========================================================================
 * F. 复位流程（FAULT → RESETTING）
 * ========================================================================= */

void test_fan_reset_requires_fault(void)
{
    fan_init(&s_ops, &s_cfg);
    /* 非 FAULT 态调用 fan_reset() 应拒绝 */
    TEST_ASSERT_EQUAL(SW_ERR_STATE, fan_reset());
}

void test_fan_reset_ok_in_fault(void)
{
    fan_init(&s_ops, &s_cfg);
    s_alarm = true;
    fan_tick();   /* → FAULT */
    s_alarm = false;
    TEST_ASSERT_EQUAL(SW_OK, fan_reset());
}

void test_fan_reset_state_resetting(void)
{
    fan_init(&s_ops, &s_cfg);
    s_alarm = true;
    fan_tick();   /* → FAULT */
    fan_reset();
    TEST_ASSERT_EQUAL(FAN_STATE_RESETTING, fan_state());
}

void test_fan_reset_activates_reset_do(void)
{
    fan_init(&s_ops, &s_cfg);
    s_alarm = true;
    fan_tick();   /* → FAULT */
    s_alarm = false;
    fan_reset();
    TEST_ASSERT_TRUE(s_reset_on);
}

void test_fan_tick_before_timeout_stays_resetting(void)
{
    fan_init(&s_ops, &s_cfg);
    s_alarm = true;
    fan_tick();   /* → FAULT */
    s_alarm = false;
    fan_reset();
    /* 立即 tick，elapsed << reset_pulse_ms(10ms)，应仍停留 RESETTING */
    fan_tick();
    TEST_ASSERT_EQUAL(FAN_STATE_RESETTING, fan_state());
}

void test_fan_resetting_blocks_start(void)
{
    fan_init(&s_ops, &s_cfg);
    s_alarm = true;
    fan_tick();
    s_alarm = false;
    fan_reset();
    TEST_ASSERT_EQUAL(SW_ERR_STATE, fan_start());
}

void test_fan_resetting_blocks_stop(void)
{
    fan_init(&s_ops, &s_cfg);
    s_alarm = true;
    fan_tick();
    s_alarm = false;
    fan_reset();
    TEST_ASSERT_EQUAL(SW_ERR_STATE, fan_stop());
}

/* =========================================================================
 * G. 复位脉冲超时后恢复 IDLE（报警已消）
 * ========================================================================= */

void test_fan_reset_timeout_alarm_clear_goes_idle(void)
{
    fan_init(&s_ops, &s_cfg);
    s_alarm = true;
    fan_tick();   /* → FAULT */
    s_alarm = false;
    fan_reset();  /* → RESETTING，记录 reset_start 时间戳 */

    /* 等待超过 reset_pulse_ms（10ms），确保 time_elapsed_ms >= 10ms */
    usleep(20000);

    fan_tick();   /* 超时：set_reset(false)，报警已消 → IDLE */
    TEST_ASSERT_EQUAL(FAN_STATE_IDLE, fan_state());
}

void test_fan_reset_timeout_deactivates_reset_do(void)
{
    fan_init(&s_ops, &s_cfg);
    s_alarm = true;
    fan_tick();
    s_alarm = false;
    fan_reset();
    usleep(20000);
    fan_tick();
    TEST_ASSERT_FALSE(s_reset_on);
}

/* =========================================================================
 * H. 复位脉冲超时后继续 FAULT（报警未消）
 * ========================================================================= */

void test_fan_reset_timeout_alarm_active_stays_fault(void)
{
    fan_init(&s_ops, &s_cfg);
    s_alarm = true;
    fan_tick();   /* → FAULT */
    fan_reset();  /* → RESETTING */

    usleep(20000);

    /* s_alarm 仍为 true，超时后应回到 FAULT */
    fan_tick();
    TEST_ASSERT_EQUAL(FAN_STATE_FAULT, fan_state());
}

void test_fan_reset_timeout_alarm_active_reset_do_off(void)
{
    fan_init(&s_ops, &s_cfg);
    s_alarm = true;
    fan_tick();
    fan_reset();
    usleep(20000);
    fan_tick();
    /* 无论报警是否消除，脉冲超时后 FAN_RESET DO 都应断开 */
    TEST_ASSERT_FALSE(s_reset_on);
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
    RUN_TEST(test_fan_not_init_reset);
    RUN_TEST(test_fan_not_init_state_idle);
    RUN_TEST(test_fan_not_init_tick_safe);

    /* A. 初始化参数校验 */
    RUN_TEST(test_fan_init_null_ops);
    RUN_TEST(test_fan_init_null_cfg);
    RUN_TEST(test_fan_init_null_set_start);
    RUN_TEST(test_fan_init_null_set_reset);
    RUN_TEST(test_fan_init_null_read_alarm);
    RUN_TEST(test_fan_init_zero_reset_pulse);
    RUN_TEST(test_fan_init_ok);
    RUN_TEST(test_fan_state_idle_after_init);

    /* C. 启动与停止 */
    RUN_TEST(test_fan_start_returns_ok);
    RUN_TEST(test_fan_start_state_running);
    RUN_TEST(test_fan_start_activates_do);
    RUN_TEST(test_fan_stop_returns_ok);
    RUN_TEST(test_fan_stop_state_idle);
    RUN_TEST(test_fan_stop_deactivates_do);
    RUN_TEST(test_fan_stop_from_idle_ok);

    /* D. 报警检测 → FAULT */
    RUN_TEST(test_fan_tick_alarm_in_running_goes_fault);
    RUN_TEST(test_fan_tick_alarm_stops_start_do);
    RUN_TEST(test_fan_tick_alarm_in_idle_goes_fault);
    RUN_TEST(test_fan_tick_no_alarm_stays_running);

    /* E. 故障态命令拒绝 */
    RUN_TEST(test_fan_fault_blocks_start);
    RUN_TEST(test_fan_fault_blocks_stop);

    /* F. 复位流程 */
    RUN_TEST(test_fan_reset_requires_fault);
    RUN_TEST(test_fan_reset_ok_in_fault);
    RUN_TEST(test_fan_reset_state_resetting);
    RUN_TEST(test_fan_reset_activates_reset_do);
    RUN_TEST(test_fan_tick_before_timeout_stays_resetting);
    RUN_TEST(test_fan_resetting_blocks_start);
    RUN_TEST(test_fan_resetting_blocks_stop);

    /* G. 超时后 → IDLE */
    RUN_TEST(test_fan_reset_timeout_alarm_clear_goes_idle);
    RUN_TEST(test_fan_reset_timeout_deactivates_reset_do);

    /* H. 超时后 → FAULT */
    RUN_TEST(test_fan_reset_timeout_alarm_active_stays_fault);
    RUN_TEST(test_fan_reset_timeout_alarm_active_reset_do_off);

    return UNITY_END();
}
