/**
 * @file    test_motor_exec.c
 * @brief   电机执行器命令与查询（motor_exec_*）单元测试。
 */

#include "common/sw_error.h"
#include "domain/mechanism/motor/motor_executor.h"
#include "domain/mechanism/motor/motor_exec.h"
#include "domain/safety/safety_output_hold.h"
#include "wdf_test_spec.h"

#include <stdbool.h>
#include <string.h>

typedef struct {
    uint64_t               now_ms;
    int64_t                position;
    int                    output_count;
    int                    cutoff_count;
    int                    current;
    int                    poll_count;
    int                    poll_motor;
    motor_speed_t          last_speed;
    motor_prepare_result_t poll_result;
    bool                   running;
    int                    request_stop_count;
    sw_err_t               request_stop_rc;
    bool                   reset_ok;
    int                    reset_count;
    int                    enc_raw_calls;
    int                    temperature;
    bool                   temperature_ok;
    motor_port_status_t    port_status;
} port_fixture_t;

static port_fixture_t   s_fx;
static motor_exec_t    *s_exec;
static motor_driver_t   s_driver;
static motor_encoder_t  s_encoder;
static motor_sensors_t  s_sensors;
static motor_clock_t    s_clock;
static motor_driver_t  *s_drivers[2];
static motor_encoder_t *s_encoders[2];
static motor_config_t   s_cfg;
static motor_ports_t    s_ports;
static bool             s_hold_di;

static bool hold_di_active(void)
{
    return s_hold_di;
}

static uint64_t clock_now(void *ctx)
{
    return ((port_fixture_t *)ctx)->now_ms;
}

static sw_err_t driver_set_output(void *ctx, motor_speed_t speed, motor_dir_t dir)
{
    port_fixture_t *fx = (port_fixture_t *)ctx;
    (void)dir;
    fx->output_count++;
    fx->last_speed = speed;
    return SW_OK;
}

static sw_err_t driver_cutoff(void *ctx)
{
    ((port_fixture_t *)ctx)->cutoff_count++;
    return SW_OK;
}

static bool driver_reset(void *ctx)
{
    port_fixture_t *fx = (port_fixture_t *)ctx;

    fx->reset_count++;
    return fx->reset_ok;
}

static bool driver_is_running(void *ctx)
{
    return ((port_fixture_t *)ctx)->running;
}

static sw_err_t driver_request_stop(void *ctx)
{
    port_fixture_t *fx = (port_fixture_t *)ctx;

    fx->request_stop_count++;
    return fx->request_stop_rc;
}

static int driver_current(void *ctx)
{
    return ((port_fixture_t *)ctx)->current;
}

static bool driver_temperature(void *ctx, int *out)
{
    port_fixture_t *fx = (port_fixture_t *)ctx;

    if (out != NULL) {
        *out = fx->temperature;
    }
    return fx->temperature_ok;
}

static motor_port_status_t driver_status(void *ctx)
{
    return ((port_fixture_t *)ctx)->port_status;
}

static motor_prepare_result_t driver_poll(void *ctx, int motor)
{
    port_fixture_t *fx = (port_fixture_t *)ctx;

    fx->poll_count++;
    fx->poll_motor = motor;
    return fx->poll_result;
}

static int64_t encoder_raw(void *ctx)
{
    ((port_fixture_t *)ctx)->enc_raw_calls++;
    return ((port_fixture_t *)ctx)->position;
}

static bool encoder_zero(void *ctx)
{
    ((port_fixture_t *)ctx)->position = 0;
    return true;
}

static bool sensor_limit(void *ctx, int motor, motor_limit_kind_t kind)
{
    (void)ctx;
    (void)motor;
    (void)kind;
    return false;
}

static void init_executor(void)
{
    motor_init_result_t ir;

    memset(&s_fx, 0, sizeof(s_fx));
    s_fx.running         = true;
    s_fx.request_stop_rc = SW_OK;
    s_fx.reset_ok        = true;
    s_fx.port_status     = MOTOR_PORT_OK;
    memset(&s_cfg, 0, sizeof(s_cfg));
    memset(&s_driver, 0, sizeof(s_driver));
    memset(&s_encoder, 0, sizeof(s_encoder));
    memset(s_drivers, 0, sizeof(s_drivers));
    memset(s_encoders, 0, sizeof(s_encoders));
    motor_executor_test_reset();
    safety_output_hold_reset();
    s_hold_di = false;
    s_exec    = NULL;

    s_clock.now_ms = clock_now;
    s_clock.ctx    = &s_fx;

    s_driver.set_output = driver_set_output;
    s_driver.cutoff     = driver_cutoff;
    s_driver.reset      = driver_reset;
    s_driver.is_running = driver_is_running;
    s_driver.current    = driver_current;
    s_driver.ctx        = &s_fx;
    s_drivers[0]        = &s_driver;

    s_encoder.raw  = encoder_raw;
    s_encoder.zero = encoder_zero;
    s_encoder.ctx  = &s_fx;
    s_encoders[0]  = &s_encoder;

    s_sensors.limit = sensor_limit;
    s_sensors.ctx   = &s_fx;

    s_ports.clock    = &s_clock;
    s_ports.drivers  = s_drivers;
    s_ports.encoders = s_encoders;
    s_ports.sensors  = &s_sensors;

    s_cfg.motor_count                   = 1;
    s_cfg.driver_count                  = 1;
    s_cfg.tick_ms                       = 10;
    s_cfg.watchdog_ms                   = 1000;
    s_cfg.motors[0].driver_index        = 0;
    s_cfg.motors[0].has_encoder         = true;
    s_cfg.motors[0].encoder_kind        = MOTOR_ENC_INCREMENTAL;
    s_cfg.motors[0].default_max_time_ms = 5000;
    s_cfg.motors[0].gear_count          = 5;
    s_cfg.motors[0].pos_tolerance       = 10;
    s_cfg.motors[0].home_dir            = MOTOR_HOME_DEFAULT_DIR;

    ir = motor_executor_bind(0U, &s_cfg, &s_ports, &s_exec);
    TEST_ASSERT_TRUE(ir.ok);
}

static void rebind_executor(void)
{
    motor_init_result_t result;

    motor_executor_test_reset();
    safety_output_hold_reset();
    s_exec = NULL;
    result = motor_executor_bind(0U, &s_cfg, &s_ports, &s_exec);
    TEST_ASSERT_TRUE_MESSAGE(result.ok, result.error);
}

static void configure_two_motors_without_encoders(void)
{
    s_cfg.motor_count           = 2;
    s_cfg.motors[0].has_encoder = false;
    s_cfg.motors[1]             = s_cfg.motors[0];
    s_encoders[0]               = NULL;
    s_encoders[1]               = NULL;
    rebind_executor();
}

/**
 * @brief 启用过流监测（确认时间 20ms = 两拍）
 */
static void enable_overcurrent_monitor(void)
{
    s_cfg.motors[0].mon.monitor_current = true;
    s_cfg.motors[0].mon.cur_max_accel   = 500;
    s_cfg.motors[0].mon.cur_max_steady  = 500;
    s_cfg.motors[0].mon.cur_min_accel   = 0;
    s_cfg.motors[0].mon.cur_min_steady  = 0;
    s_cfg.motors[0].mon.cur_confirm_ms  = 20;
}

/**
 * @brief 运行至过流 FAULT
 */
static void run_until_overcurrent(motor_exec_t *hal)
{
    motor_cmd_result_t r;

    s_fx.current = 600;
    r            = motor_exec_run(hal, 0, motor_speed_gear(1), MOTOR_DIR_FORWARD, NULL);
    TEST_ASSERT_TRUE(motor_cmd_ok(r));
    s_fx.now_ms = 10;
    motor_executor_tick(s_exec);
    s_fx.now_ms = 20;
    motor_executor_tick(s_exec);
    TEST_ASSERT_EQUAL_INT(MOTOR_STATE_FAULT, motor_exec_state(hal, 0));
    TEST_ASSERT_EQUAL_INT(MOTOR_FAULT_OVERCURRENT, motor_exec_fault_code(hal, 0));
}

void setUp(void)
{
    init_executor();
}

void tearDown(void)
{
}

static void test_run_continuous_and_state_via_port(void)
{
    motor_cmd_result_t r;
    motor_exec_t      *hal = s_exec;

    r = motor_exec_run(hal, 0, motor_speed_gear(2), MOTOR_DIR_REVERSE, NULL);
    TEST_ASSERT_TRUE(motor_cmd_ok(r));
    motor_executor_tick(s_exec);
    TEST_ASSERT_EQUAL_INT(MOTOR_STATE_RUNNING, motor_exec_state(hal, 0));
    TEST_ASSERT_EQUAL_INT(MOTOR_DIR_REVERSE, motor_exec_direction(hal, 0));
    TEST_ASSERT_TRUE(s_fx.output_count > 0);
}

static void test_slot_binding_is_fixed_and_reinit_keeps_handle(void)
{
    motor_exec_t       *duplicate = s_exec;
    motor_exec_t       *original  = s_exec;
    motor_init_result_t result;

    result = motor_executor_bind(0U, &s_cfg, &s_ports, &duplicate);
    TEST_ASSERT_FALSE(result.ok);
    TEST_ASSERT_EQUAL_STRING("slot already bound", result.error);
    TEST_ASSERT_NULL(duplicate);

    result = motor_executor_bind(WDF_MOTOR_EXECUTOR_INSTANCE_COUNT, &s_cfg, &s_ports, &duplicate);
    TEST_ASSERT_FALSE(result.ok);
    TEST_ASSERT_EQUAL_STRING("slot id out of range", result.error);
    TEST_ASSERT_NULL(duplicate);

    result = motor_executor_reinit(original);
    TEST_ASSERT_TRUE_MESSAGE(result.ok, result.error);
    TEST_ASSERT_EQUAL_PTR(original, s_exec);
    TEST_ASSERT_EQUAL_INT(MOTOR_STATE_STOPPED, motor_exec_state(s_exec, 0));
}

static void test_move_to_time_and_stop_via_port(void)
{
    motor_move_spec_t  spec;
    motor_cmd_result_t r;
    motor_exec_t      *hal = s_exec;

    memset(&spec, 0, sizeof(spec));
    spec.use_time    = true;
    spec.duration_ms = 30;
    spec.max_time_ms = 1000;

    r = motor_exec_run(hal, 0, motor_speed_freq(1000), MOTOR_DIR_FORWARD, &spec);
    TEST_ASSERT_TRUE(motor_cmd_ok(r));
    motor_executor_tick(s_exec);
    TEST_ASSERT_EQUAL_INT(MOTOR_STATE_RUNNING, motor_exec_state(hal, 0));

    r = motor_exec_stop(hal, 0);
    TEST_ASSERT_TRUE(motor_cmd_ok(r));
    motor_executor_tick(s_exec);
    TEST_ASSERT_EQUAL_INT(MOTOR_STATE_STOPPED, motor_exec_state(hal, 0));
}

static void test_run_updates_speed_without_restart(void)
{
    motor_move_spec_t  spec;
    motor_cmd_result_t r;
    motor_exec_t      *hal = s_exec;

    memset(&spec, 0, sizeof(spec));
    spec.use_time    = true;
    spec.duration_ms = 30;
    spec.max_time_ms = 1000;

    r = motor_exec_run(hal, 0, motor_speed_gear(1), MOTOR_DIR_FORWARD, &spec);
    TEST_ASSERT_TRUE(motor_cmd_ok(r));
    motor_executor_tick(s_exec);
    TEST_ASSERT_EQUAL_INT(MOTOR_STATE_RUNNING, motor_exec_state(hal, 0));

    s_fx.now_ms = 20;
    r           = motor_exec_run(hal, 0, motor_speed_gear(3), MOTOR_DIR_FORWARD, &spec);
    TEST_ASSERT_TRUE(motor_cmd_ok(r));
    TEST_ASSERT_EQUAL_STRING("goal-updated", r.reason);
    TEST_ASSERT_EQUAL_INT(MOTOR_STATE_RUNNING, motor_exec_state(hal, 0));
    motor_executor_tick(s_exec);
    TEST_ASSERT_EQUAL_INT(2, s_fx.output_count);
    TEST_ASSERT_EQUAL_INT(3, s_fx.last_speed.value);

    s_fx.now_ms = 30;
    motor_executor_tick(s_exec);
    TEST_ASSERT_EQUAL_INT(MOTOR_STATE_STOPPED, motor_exec_state(hal, 0));
}

static void test_run_updates_pending_while_waiting_start(void)
{
    motor_cmd_result_t r;
    motor_exec_t      *hal;

    s_cfg.motors[0].cooldown_ms = 100;
    rebind_executor();
    hal = s_exec;
    r   = motor_exec_run(hal, 0, motor_speed_gear(1), MOTOR_DIR_FORWARD, NULL);
    TEST_ASSERT_TRUE(motor_cmd_ok(r));
    motor_executor_tick(s_exec);
    r = motor_exec_stop(hal, 0);
    TEST_ASSERT_TRUE(motor_cmd_ok(r));
    motor_executor_tick(s_exec);
    TEST_ASSERT_EQUAL_INT(MOTOR_STATE_STOPPED, motor_exec_state(hal, 0));

    r = motor_exec_run(hal, 0, motor_speed_gear(1), MOTOR_DIR_FORWARD, NULL);
    TEST_ASSERT_TRUE(motor_cmd_ok(r));
    TEST_ASSERT_EQUAL_INT(MOTOR_CMD_QUEUED, r.status);
    TEST_ASSERT_EQUAL_INT(MOTOR_STATE_WAITING_START, motor_exec_state(hal, 0));

    r = motor_exec_run(hal, 0, motor_speed_gear(4), MOTOR_DIR_FORWARD, NULL);
    TEST_ASSERT_TRUE(motor_cmd_ok(r));
    TEST_ASSERT_EQUAL_STRING("pending-updated", r.reason);
    TEST_ASSERT_EQUAL_INT(MOTOR_STATE_WAITING_START, motor_exec_state(hal, 0));

    s_fx.now_ms = 100;
    motor_executor_tick(s_exec);
    s_fx.now_ms = 110;
    motor_executor_tick(s_exec);
    TEST_ASSERT_EQUAL_INT(MOTOR_STATE_RUNNING, motor_exec_state(hal, 0));
    TEST_ASSERT_EQUAL_INT(4, s_fx.last_speed.value);
}

static void test_recover_via_port(void)
{
    motor_cmd_result_t r;
    motor_exec_t      *hal;

    enable_overcurrent_monitor();
    rebind_executor();
    hal = s_exec;
    run_until_overcurrent(hal);

    r = motor_exec_recover(hal, 0, MOTOR_RECOVERY_DRIVER_RESET);
    TEST_ASSERT_TRUE(motor_cmd_ok(r));
    r = motor_exec_recover(hal, 0, MOTOR_RECOVERY_MODULE_STOP);
    TEST_ASSERT_TRUE(motor_cmd_ok(r));
    TEST_ASSERT_EQUAL_INT(MOTOR_STATE_STOPPED, motor_exec_state(hal, 0));
}

static void test_query_helpers_via_port(void)
{
    motor_cmd_result_t result;
    motor_exec_t      *hal;

    s_fx.position                = 42;
    s_cfg.motors[0].encoder_kind = MOTOR_ENC_ABSOLUTE;
    rebind_executor();
    hal = s_exec;

    TEST_ASSERT_EQUAL_INT64(42, motor_exec_position(hal, 0));
    TEST_ASSERT_TRUE(motor_exec_baseline_trusted(hal, 0));
    TEST_ASSERT_TRUE(motor_exec_encoder_healthy(hal, 0));

    s_fx.position                   = 0;
    s_cfg.motors[0].encoder_kind    = MOTOR_ENC_INCREMENTAL;
    s_cfg.motors[0].enc_stall_ticks = 1;
    rebind_executor();
    hal    = s_exec;
    result = motor_exec_confirm_baseline(hal, 0);
    TEST_ASSERT_TRUE(motor_cmd_ok(result));
    result = motor_exec_run(hal, 0, motor_speed_gear(1), MOTOR_DIR_FORWARD, NULL);
    TEST_ASSERT_TRUE(motor_cmd_ok(result));
    s_fx.now_ms = 10;
    motor_executor_tick(s_exec);
    TEST_ASSERT_TRUE(motor_exec_baseline_trusted(hal, 0));
    TEST_ASSERT_FALSE(motor_exec_encoder_healthy(hal, 0));

    /* 无编码器时查询侧恒 true。 */
    s_cfg.motors[0].has_encoder = false;
    s_encoders[0]               = NULL;
    rebind_executor();
    hal = s_exec;
    TEST_ASSERT_TRUE(motor_exec_baseline_trusted(hal, 0));
    TEST_ASSERT_TRUE(motor_exec_encoder_healthy(hal, 0));
}

/**
 * @brief 增量轴空闲不读编码器，避免同步占用脉冲通道
 */
static void test_incremental_idle_skips_encoder_raw(void)
{
    int after_init = s_fx.enc_raw_calls;

    TEST_ASSERT_GREATER_THAN_INT(0, after_init);
    motor_executor_tick(s_exec);
    motor_executor_tick(s_exec);
    TEST_ASSERT_EQUAL_INT(after_init, s_fx.enc_raw_calls);
}

static void test_query_helpers_return_safe_defaults_for_bad_motor(void)
{
    TEST_ASSERT_EQUAL_INT(MOTOR_STATE_STOPPED, motor_exec_state(s_exec, -1));
    TEST_ASSERT_EQUAL_INT(MOTOR_STATE_STOPPED, motor_exec_state(s_exec, 1));
    TEST_ASSERT_EQUAL_INT64(0, motor_exec_position(s_exec, -1));
    TEST_ASSERT_EQUAL_INT64(0, motor_exec_position(s_exec, 1));
    TEST_ASSERT_EQUAL_INT(MOTOR_DIR_FORWARD, motor_exec_direction(s_exec, -1));
    TEST_ASSERT_EQUAL_INT(MOTOR_DIR_FORWARD, motor_exec_direction(s_exec, 1));
    TEST_ASSERT_EQUAL_INT(MOTOR_FAULT_NONE, motor_exec_fault_code(s_exec, -1));
    TEST_ASSERT_EQUAL_INT(MOTOR_FAULT_NONE, motor_exec_fault_code(s_exec, 1));
    TEST_ASSERT_TRUE(motor_exec_fault_requires_confirm(s_exec, -1, MOTOR_FAULT_OVERCURRENT));
    TEST_ASSERT_TRUE(motor_exec_fault_requires_confirm(s_exec, 1, MOTOR_FAULT_OVERCURRENT));
    TEST_ASSERT_TRUE(motor_exec_fault_requires_confirm(NULL, 0, MOTOR_FAULT_OVERCURRENT));
    TEST_ASSERT_FALSE(motor_exec_baseline_trusted(s_exec, -1));
    TEST_ASSERT_FALSE(motor_exec_baseline_trusted(s_exec, 1));
    TEST_ASSERT_FALSE(motor_exec_encoder_healthy(s_exec, -1));
    TEST_ASSERT_FALSE(motor_exec_encoder_healthy(s_exec, 1));
    TEST_ASSERT_EQUAL_INT(0, motor_exec_current_freq(s_exec, -1));
    TEST_ASSERT_EQUAL_INT(0, motor_exec_current_freq(s_exec, 1));
}

static void test_run_rejects_unset_dir(void)
{
    motor_cmd_result_t r;

    r = motor_exec_run(s_exec, 0, motor_speed_gear(1), MOTOR_DIR_UNSET, NULL);
    TEST_ASSERT_FALSE(motor_cmd_ok(r));
    TEST_ASSERT_EQUAL_INT(MOTOR_REJECT_BAD_DIR, r.reject);
    TEST_ASSERT_EQUAL_INT(MOTOR_STATE_STOPPED, motor_exec_state(s_exec, 0));
}

static void test_output_hold_cuts_and_rejects_until_release(void)
{
    motor_cmd_result_t r;

    r = motor_exec_run(s_exec, 0, motor_speed_gear(1), MOTOR_DIR_FORWARD, NULL);
    TEST_ASSERT_TRUE(motor_cmd_ok(r));
    motor_executor_tick(s_exec);
    TEST_ASSERT_EQUAL_INT(MOTOR_STATE_RUNNING, motor_exec_state(s_exec, 0));

    safety_output_hold_request();
    motor_executor_tick(s_exec);
    TEST_ASSERT_EQUAL_INT(MOTOR_STATE_ESTOP, motor_exec_state(s_exec, 0));

    r = motor_exec_run(s_exec, 0, motor_speed_gear(1), MOTOR_DIR_FORWARD, NULL);
    TEST_ASSERT_FALSE(motor_cmd_ok(r));
    TEST_ASSERT_EQUAL_INT(MOTOR_REJECT_SAFETY, r.reject);

    TEST_ASSERT_EQUAL_INT(SW_OK, safety_output_hold_release());
    motor_executor_tick(s_exec);
    TEST_ASSERT_EQUAL_INT(MOTOR_STATE_STOPPED, motor_exec_state(s_exec, 0));
    TEST_ASSERT_FALSE(safety_output_hold_is_active());

    r = motor_exec_run(s_exec, 0, motor_speed_gear(1), MOTOR_DIR_FORWARD, NULL);
    TEST_ASSERT_TRUE(motor_cmd_ok(r));
}

static void test_output_hold_reset_rejected_while_di_active(void)
{
    s_hold_di = true;
    safety_output_hold_bind_di(hold_di_active);
    motor_executor_tick(s_exec);
    TEST_ASSERT_EQUAL_INT(MOTOR_STATE_ESTOP, motor_exec_state(s_exec, 0));

    TEST_ASSERT_EQUAL_INT(SW_ERR_STATE, safety_output_hold_release());
    motor_executor_tick(s_exec);
    TEST_ASSERT_EQUAL_INT(MOTOR_STATE_ESTOP, motor_exec_state(s_exec, 0));

    s_hold_di = false;
    TEST_ASSERT_EQUAL_INT(SW_OK, safety_output_hold_release());
    motor_executor_tick(s_exec);
    TEST_ASSERT_EQUAL_INT(MOTOR_STATE_STOPPED, motor_exec_state(s_exec, 0));
}

static void test_pop_event_via_port(void)
{
    motor_move_spec_t spec;
    motor_event_t     out;
    motor_exec_t     *hal = s_exec;

    memset(&spec, 0, sizeof(spec));
    spec.use_time    = true;
    spec.duration_ms = 20;
    spec.max_time_ms = 1000;
    TEST_ASSERT_TRUE(motor_cmd_ok(motor_exec_run(hal, 0, motor_speed_gear(1), MOTOR_DIR_FORWARD, &spec)));
    s_fx.position = 10;
    s_fx.now_ms   = 10;
    motor_executor_tick(s_exec);
    s_fx.now_ms = 20;
    motor_executor_tick(s_exec);

    TEST_ASSERT_TRUE(motor_exec_pop_event(hal, &out));
    TEST_ASSERT_EQUAL_INT(MOTOR_EVENT_ARRIVED, out.type);
    TEST_ASSERT_EQUAL_INT(MOTOR_END_TIME, out.trigger);
    TEST_ASSERT_EQUAL_INT64(10, out.final_pos);
    TEST_ASSERT_EQUAL_UINT64(20, out.elapsed_ms);
    TEST_ASSERT_TRUE(out.use_time);
    TEST_ASSERT_FALSE(out.use_position);
    TEST_ASSERT_EQUAL_UINT8(0, out.limit_mask);
    TEST_ASSERT_FALSE(motor_exec_pop_event(hal, &out));
    TEST_ASSERT_FALSE(motor_exec_pop_event(hal, NULL));
}

/**
 * @brief 超时结束事件带上本次监视限位与到位方式，供上层区分限位超时与动作超时
 */
static void test_timeout_event_copies_move_spec(void)
{
    motor_move_spec_t spec;
    motor_event_t     out;
    motor_exec_t     *hal = s_exec;

    memset(&spec, 0, sizeof(spec));
    spec.limit_mask  = MOTOR_LIMIT_MASK_POS;
    spec.max_time_ms = 20;

    TEST_ASSERT_TRUE(motor_cmd_ok(motor_exec_run(hal, 0, motor_speed_gear(1), MOTOR_DIR_FORWARD, &spec)));
    s_fx.now_ms = 10;
    motor_executor_tick(s_exec);
    s_fx.now_ms = 20;
    motor_executor_tick(s_exec);

    TEST_ASSERT_TRUE(motor_exec_pop_event(hal, &out));
    TEST_ASSERT_EQUAL_INT(MOTOR_EVENT_TIMEOUT, out.type);
    TEST_ASSERT_EQUAL_UINT8(MOTOR_LIMIT_MASK_POS, out.limit_mask);
    TEST_ASSERT_FALSE(out.use_position);
    TEST_ASSERT_FALSE(out.use_time);
}

/**
 * @brief 过流故障事件带上采样电流与触发阈值
 */
static void test_overcurrent_event_copies_current(void)
{
    motor_event_t out;
    motor_exec_t *hal;

    enable_overcurrent_monitor();
    rebind_executor();
    hal = s_exec;
    run_until_overcurrent(hal);

    TEST_ASSERT_TRUE(motor_exec_pop_event(hal, &out));
    TEST_ASSERT_EQUAL_INT(MOTOR_EVENT_FAULT, out.type);
    TEST_ASSERT_EQUAL_INT(MOTOR_FAULT_OVERCURRENT, out.fault);
    TEST_ASSERT_EQUAL_INT(600, out.current);
    TEST_ASSERT_EQUAL_INT(500, out.current_limit);
}

static void test_pop_event_for_keeps_other_motors(void)
{
    motor_move_spec_t spec;
    motor_event_t     out;
    motor_exec_t     *hal;

    configure_two_motors_without_encoders();
    hal = s_exec;
    memset(&spec, 0, sizeof(spec));
    spec.use_time    = true;
    spec.duration_ms = 10;
    spec.max_time_ms = 1000;

    TEST_ASSERT_TRUE(motor_cmd_ok(motor_exec_run(hal, 1, motor_speed_gear(1), MOTOR_DIR_FORWARD, &spec)));
    s_fx.now_ms = 10;
    motor_executor_tick(s_exec);
    TEST_ASSERT_TRUE(motor_cmd_ok(motor_exec_run(hal, 0, motor_speed_gear(1), MOTOR_DIR_FORWARD, &spec)));
    s_fx.now_ms = 20;
    motor_executor_tick(s_exec);

    TEST_ASSERT_TRUE(motor_exec_pop_event_for(hal, 0, &out));
    TEST_ASSERT_EQUAL_INT(0, out.motor);
    TEST_ASSERT_EQUAL_INT(MOTOR_END_TIME, out.trigger);

    TEST_ASSERT_FALSE(motor_exec_pop_event_for(hal, 0, &out));
    TEST_ASSERT_TRUE(motor_exec_pop_event(hal, &out));
    TEST_ASSERT_EQUAL_INT(1, out.motor);
    TEST_ASSERT_EQUAL_INT(MOTOR_END_TIME, out.trigger);
}

/**
 * @brief 队列满时优先丢同电机最旧，不挤掉其它电机未消费事件
 */
static void test_event_queue_prefers_drop_same_motor(void)
{
    motor_move_spec_t spec;
    motor_event_t     out;
    motor_exec_t     *hal;
    int               i;
    int               motor0_left;
    int               motor1_left;

    configure_two_motors_without_encoders();
    hal = s_exec;
    memset(&spec, 0, sizeof(spec));
    spec.use_time    = true;
    spec.duration_ms = 0;
    spec.max_time_ms = 1000;

    for (i = 0; i < MOTOR_EVENT_SLOT_CAP; ++i) {
        TEST_ASSERT_TRUE(motor_cmd_ok(motor_exec_run(hal, 1, motor_speed_gear(1), MOTOR_DIR_FORWARD, &spec)));
        motor_executor_tick(s_exec);
    }
    for (i = 0; i < MOTOR_EVENT_SLOT_CAP; ++i) {
        TEST_ASSERT_TRUE(motor_cmd_ok(motor_exec_run(hal, 0, motor_speed_gear(1), MOTOR_DIR_FORWARD, &spec)));
        motor_executor_tick(s_exec);
    }

    TEST_ASSERT_TRUE(motor_cmd_ok(motor_exec_run(hal, 0, motor_speed_gear(1), MOTOR_DIR_FORWARD, &spec)));
    motor_executor_tick(s_exec);

    motor0_left = 0;
    motor1_left = 0;
    while (motor_exec_pop_event(hal, &out)) {
        if (out.motor == 0) {
            motor0_left++;
        } else if (out.motor == 1) {
            motor1_left++;
        }
    }
    TEST_ASSERT_EQUAL_INT(MOTOR_EVENT_SLOT_CAP, motor1_left);
    TEST_ASSERT_EQUAL_INT(MOTOR_EVENT_SLOT_CAP, motor0_left);
}

static void tick_ms(uint64_t now_ms)
{
    s_fx.now_ms = now_ms;
    motor_executor_tick(s_exec);
}

static void start_running_at(uint64_t now_ms)
{
    motor_cmd_result_t r;

    r = motor_exec_run(s_exec, 0, motor_speed_gear(1), MOTOR_DIR_FORWARD, NULL);
    TEST_ASSERT_TRUE(motor_cmd_ok(r));
    tick_ms(now_ms);
    TEST_ASSERT_EQUAL_INT(MOTOR_STATE_RUNNING, motor_exec_state(s_exec, 0));
}

static void bind_with_request_stop(int stop_timeout_ms)
{
    s_driver.request_stop           = driver_request_stop;
    s_cfg.motors[0].stop_timeout_ms = stop_timeout_ms;
    rebind_executor();
    s_fx.running            = true;
    s_fx.request_stop_rc    = SW_OK;
    s_fx.request_stop_count = 0;
}

static void test_request_stop_waits_until_driver_idle(void)
{
    motor_cmd_result_t r;
    int                cutoff0;

    bind_with_request_stop(100);
    start_running_at(10);
    cutoff0 = s_fx.cutoff_count;

    r = motor_exec_stop(s_exec, 0);
    TEST_ASSERT_TRUE(motor_cmd_ok(r));
    TEST_ASSERT_EQUAL_STRING("stopping", r.reason);
    tick_ms(20);
    TEST_ASSERT_EQUAL_INT(MOTOR_STATE_STOPPING, motor_exec_state(s_exec, 0));
    TEST_ASSERT_EQUAL_INT(1, s_fx.request_stop_count);
    TEST_ASSERT_EQUAL_INT(cutoff0, s_fx.cutoff_count);

    s_fx.running = false;
    tick_ms(30);
    TEST_ASSERT_EQUAL_INT(MOTOR_STATE_STOPPED, motor_exec_state(s_exec, 0));
    TEST_ASSERT_EQUAL_INT(1, s_fx.request_stop_count);
    TEST_ASSERT_TRUE(s_fx.cutoff_count > cutoff0);
}

static void test_request_stop_timeout_force_cutoff(void)
{
    bind_with_request_stop(30);
    start_running_at(10);
    TEST_ASSERT_TRUE(motor_cmd_ok(motor_exec_stop(s_exec, 0)));
    tick_ms(20);
    TEST_ASSERT_EQUAL_INT(MOTOR_STATE_STOPPING, motor_exec_state(s_exec, 0));
    tick_ms(40);
    TEST_ASSERT_EQUAL_INT(MOTOR_STATE_STOPPING, motor_exec_state(s_exec, 0));
    tick_ms(50);
    TEST_ASSERT_EQUAL_INT(MOTOR_STATE_STOPPED, motor_exec_state(s_exec, 0));
}

static void test_repeat_stop_does_not_restart_timeout(void)
{
    bind_with_request_stop(30);
    start_running_at(10);
    TEST_ASSERT_TRUE(motor_cmd_ok(motor_exec_stop(s_exec, 0)));
    tick_ms(20);
    TEST_ASSERT_EQUAL_INT(1, s_fx.request_stop_count);
    s_fx.now_ms = 25;
    TEST_ASSERT_TRUE(motor_cmd_ok(motor_exec_stop(s_exec, 0)));
    TEST_ASSERT_EQUAL_INT(MOTOR_STATE_STOPPING, motor_exec_state(s_exec, 0));
    tick_ms(50);
    TEST_ASSERT_EQUAL_INT(MOTOR_STATE_STOPPED, motor_exec_state(s_exec, 0));
    TEST_ASSERT_EQUAL_INT(1, s_fx.request_stop_count);
}

static void test_request_stop_failure_cuts_off(void)
{
    int cutoff0;

    bind_with_request_stop(100);
    start_running_at(10);
    cutoff0              = s_fx.cutoff_count;
    s_fx.request_stop_rc = SW_ERR_HW;
    TEST_ASSERT_TRUE(motor_cmd_ok(motor_exec_stop(s_exec, 0)));
    tick_ms(20);
    TEST_ASSERT_EQUAL_INT(MOTOR_STATE_STOPPED, motor_exec_state(s_exec, 0));
    TEST_ASSERT_EQUAL_INT(1, s_fx.request_stop_count);
    TEST_ASSERT_TRUE(s_fx.cutoff_count > cutoff0);
}

static void test_run_during_stopping_starts_after_halt(void)
{
    motor_cmd_result_t r;

    bind_with_request_stop(30);
    start_running_at(10);
    TEST_ASSERT_TRUE(motor_cmd_ok(motor_exec_stop(s_exec, 0)));
    tick_ms(20);
    TEST_ASSERT_EQUAL_INT(MOTOR_STATE_STOPPING, motor_exec_state(s_exec, 0));

    r = motor_exec_run(s_exec, 0, motor_speed_gear(2), MOTOR_DIR_REVERSE, NULL);
    TEST_ASSERT_TRUE(motor_cmd_ok(r));
    TEST_ASSERT_EQUAL_STRING("stop-then-start", r.reason);
    TEST_ASSERT_EQUAL_INT(MOTOR_STATE_STOPPING, motor_exec_state(s_exec, 0));

    s_fx.running = false;
    tick_ms(30);
    TEST_ASSERT_EQUAL_INT(MOTOR_STATE_WAITING_START, motor_exec_state(s_exec, 0));
    tick_ms(40);
    TEST_ASSERT_EQUAL_INT(MOTOR_STATE_RUNNING, motor_exec_state(s_exec, 0));
    TEST_ASSERT_EQUAL_INT(MOTOR_DIR_REVERSE, motor_exec_direction(s_exec, 0));
}

static void test_running_poll_busy_then_failed_enters_prepare_failed(void)
{
    motor_cmd_result_t r;
    motor_exec_t      *hal = s_exec;

    s_driver.poll    = driver_poll;
    s_fx.poll_result = MOTOR_PREPARE_READY;

    r = motor_exec_run(hal, 0, motor_speed_gear(1), MOTOR_DIR_FORWARD, NULL);
    TEST_ASSERT_TRUE(motor_cmd_ok(r));
    motor_executor_tick(s_exec);
    TEST_ASSERT_EQUAL_INT(MOTOR_STATE_RUNNING, motor_exec_state(hal, 0));
    TEST_ASSERT_EQUAL_INT(1, s_fx.poll_count);
    TEST_ASSERT_EQUAL_INT(0, s_fx.poll_motor);
    TEST_ASSERT_TRUE(s_fx.output_count > 0);

    s_fx.poll_result = MOTOR_PREPARE_BUSY;
    motor_executor_tick(s_exec);
    TEST_ASSERT_EQUAL_INT(MOTOR_STATE_RUNNING, motor_exec_state(hal, 0));
    TEST_ASSERT_EQUAL_INT(2, s_fx.poll_count);

    s_fx.poll_result = MOTOR_PREPARE_FAILED;
    motor_executor_tick(s_exec);
    TEST_ASSERT_EQUAL_INT(MOTOR_STATE_FAULT, motor_exec_state(hal, 0));
    TEST_ASSERT_EQUAL_INT(MOTOR_FAULT_PREPARE_FAILED, motor_exec_fault_code(hal, 0));
    TEST_ASSERT_EQUAL_INT(3, s_fx.poll_count);
    TEST_ASSERT_TRUE(s_fx.cutoff_count > 0);
}

/** @brief 默认 confirm_faults=0：过流 FAULT 后直接 run 可受理。 */
static void test_resumable_overcurrent_run_without_recover(void)
{
    motor_cmd_result_t r;
    motor_exec_t      *hal;

    enable_overcurrent_monitor();
    rebind_executor();
    hal = s_exec;
    TEST_ASSERT_FALSE(motor_exec_fault_requires_confirm(hal, 0, MOTOR_FAULT_OVERCURRENT));
    TEST_ASSERT_TRUE(motor_exec_fault_requires_confirm(hal, 0, MOTOR_FAULT_DRIVER_PORT_FATAL));
    run_until_overcurrent(hal);

    s_fx.current = 0;
    r            = motor_exec_run(hal, 0, motor_speed_gear(1), MOTOR_DIR_FORWARD, NULL);
    TEST_ASSERT_TRUE(motor_cmd_ok(r));
    TEST_ASSERT_NOT_EQUAL(MOTOR_STATE_FAULT, motor_exec_state(hal, 0));
    TEST_ASSERT_EQUAL_INT(MOTOR_FAULT_NONE, motor_exec_fault_code(hal, 0));
}

/** @brief 仅过温列入确认表：过流可续动，过温须显式 recover。 */
static void test_overtemp_confirm_rejects_until_recover(void)
{
    motor_cmd_result_t r;
    motor_exec_t      *hal;

    enable_overcurrent_monitor();
    s_cfg.motors[0].confirm_faults      = motor_fault_confirm_bit(MOTOR_FAULT_OVERTEMP);
    s_cfg.motors[0].mon.monitor_temp    = true;
    s_cfg.motors[0].mon.temp_max        = 80;
    s_cfg.motors[0].mon.temp_confirm_ms = 20;
    s_driver.temperature                = driver_temperature;
    s_fx.temperature_ok                 = true;
    s_fx.temperature                    = 0;
    rebind_executor();
    hal = s_exec;
    TEST_ASSERT_FALSE(motor_exec_fault_requires_confirm(hal, 0, MOTOR_FAULT_OVERCURRENT));
    TEST_ASSERT_TRUE(motor_exec_fault_requires_confirm(hal, 0, MOTOR_FAULT_OVERTEMP));

    run_until_overcurrent(hal);
    s_fx.current = 0;
    r            = motor_exec_run(hal, 0, motor_speed_gear(1), MOTOR_DIR_FORWARD, NULL);
    TEST_ASSERT_TRUE(motor_cmd_ok(r));
    TEST_ASSERT_EQUAL_INT(MOTOR_STATE_RUNNING, motor_exec_state(hal, 0));

    s_fx.temperature = 100;
    s_fx.now_ms      = 30;
    motor_executor_tick(s_exec);
    s_fx.now_ms = 40;
    motor_executor_tick(s_exec);
    TEST_ASSERT_EQUAL_INT(MOTOR_STATE_FAULT, motor_exec_state(hal, 0));
    TEST_ASSERT_EQUAL_INT(MOTOR_FAULT_OVERTEMP, motor_exec_fault_code(hal, 0));

    r = motor_exec_run(hal, 0, motor_speed_gear(1), MOTOR_DIR_FORWARD, NULL);
    TEST_ASSERT_FALSE(motor_cmd_ok(r));
    TEST_ASSERT_EQUAL_INT(MOTOR_REJECT_FAULT, r.reject);
    TEST_ASSERT_EQUAL_INT(MOTOR_STATE_FAULT, motor_exec_state(hal, 0));

    r = motor_exec_home(hal, 0);
    TEST_ASSERT_FALSE(motor_cmd_ok(r));
    TEST_ASSERT_EQUAL_INT(MOTOR_REJECT_FAULT, r.reject);
    TEST_ASSERT_EQUAL_INT(MOTOR_STATE_FAULT, motor_exec_state(hal, 0));

    r = motor_exec_recover(hal, 0, MOTOR_RECOVERY_DRIVER_RESET);
    TEST_ASSERT_TRUE(motor_cmd_ok(r));
    r = motor_exec_recover(hal, 0, MOTOR_RECOVERY_MODULE_STOP);
    TEST_ASSERT_TRUE(motor_cmd_ok(r));
    TEST_ASSERT_EQUAL_INT(MOTOR_STATE_STOPPED, motor_exec_state(hal, 0));

    s_fx.temperature = 0;
    r                = motor_exec_run(hal, 0, motor_speed_gear(1), MOTOR_DIR_FORWARD, NULL);
    TEST_ASSERT_TRUE(motor_cmd_ok(r));
}

/** @brief 可续动内清复位失败则保持 FAULT；仅 tick 不自复。 */
static void test_resumable_reset_fail_keeps_fault(void)
{
    motor_cmd_result_t r;
    motor_exec_t      *hal;

    enable_overcurrent_monitor();
    rebind_executor();
    hal = s_exec;
    run_until_overcurrent(hal);

    s_fx.reset_ok = false;
    r             = motor_exec_run(hal, 0, motor_speed_gear(1), MOTOR_DIR_FORWARD, NULL);
    TEST_ASSERT_FALSE(motor_cmd_ok(r));
    TEST_ASSERT_EQUAL_INT(MOTOR_REJECT_DRIVER, r.reject);
    TEST_ASSERT_EQUAL_INT(MOTOR_STATE_FAULT, motor_exec_state(hal, 0));
    TEST_ASSERT_EQUAL_INT(MOTOR_FAULT_OVERCURRENT, motor_exec_fault_code(hal, 0));

    s_fx.now_ms = 30;
    motor_executor_tick(s_exec);
    TEST_ASSERT_EQUAL_INT(MOTOR_STATE_FAULT, motor_exec_state(hal, 0));
}

/** @brief 可续动内清不得发生在方向/互锁校验失败之前。 */
static void test_resumable_reject_does_not_clear_fault(void)
{
    motor_cmd_result_t r;
    motor_exec_t      *hal;
    int                resets_after_fault;

    enable_overcurrent_monitor();
    rebind_executor();
    hal = s_exec;
    run_until_overcurrent(hal);
    resets_after_fault = s_fx.reset_count;

    r = motor_exec_run(hal, 0, motor_speed_gear(1), MOTOR_DIR_UNSET, NULL);
    TEST_ASSERT_FALSE(motor_cmd_ok(r));
    TEST_ASSERT_EQUAL_INT(MOTOR_REJECT_BAD_DIR, r.reject);
    TEST_ASSERT_EQUAL_INT(MOTOR_STATE_FAULT, motor_exec_state(hal, 0));
    TEST_ASSERT_EQUAL_INT(MOTOR_FAULT_OVERCURRENT, motor_exec_fault_code(hal, 0));
    TEST_ASSERT_EQUAL_INT(resets_after_fault, s_fx.reset_count);

    s_cfg.motor_count                   = 2;
    s_cfg.driver_count                  = 2;
    s_drivers[1]                        = &s_driver;
    s_cfg.motors[1]                     = s_cfg.motors[0];
    s_cfg.motors[1].driver_index        = 1;
    s_cfg.motors[1].mon.monitor_current = false;
    s_encoders[1]                       = &s_encoder;
    s_cfg.interlock_count               = 1;
    s_cfg.interlocks[0].kind            = MOTOR_INTERLOCK_MUTEX;
    s_cfg.interlocks[0].a               = 0;
    s_cfg.interlocks[0].b               = 1;
    s_fx.now_ms                         = 0;
    rebind_executor();
    hal = s_exec;
    run_until_overcurrent(hal);
    r = motor_exec_run(hal, 1, motor_speed_gear(1), MOTOR_DIR_FORWARD, NULL);
    TEST_ASSERT_TRUE(motor_cmd_ok(r));
    motor_executor_tick(s_exec);
    TEST_ASSERT_EQUAL_INT(MOTOR_STATE_RUNNING, motor_exec_state(hal, 1));
    TEST_ASSERT_EQUAL_INT(MOTOR_STATE_FAULT, motor_exec_state(hal, 0));
    resets_after_fault = s_fx.reset_count;

    r = motor_exec_run(hal, 0, motor_speed_gear(1), MOTOR_DIR_FORWARD, NULL);
    TEST_ASSERT_FALSE(motor_cmd_ok(r));
    TEST_ASSERT_EQUAL_INT(MOTOR_REJECT_INTERLOCK, r.reject);
    TEST_ASSERT_EQUAL_INT(MOTOR_STATE_FAULT, motor_exec_state(hal, 0));
    TEST_ASSERT_EQUAL_INT(MOTOR_FAULT_OVERCURRENT, motor_exec_fault_code(hal, 0));
    TEST_ASSERT_EQUAL_INT(resets_after_fault, s_fx.reset_count);
}

/** @brief fatal 与 hold 有效时即使位图为 0 也不得按可续动放行。 */
static void test_fatal_and_hold_not_resumable(void)
{
    motor_cmd_result_t r;
    motor_exec_t      *hal = s_exec;

    TEST_ASSERT_EQUAL_UINT32(0u, s_cfg.motors[0].confirm_faults);
    s_driver.status  = driver_status;
    s_fx.port_status = MOTOR_PORT_OK;
    r                = motor_exec_run(hal, 0, motor_speed_gear(1), MOTOR_DIR_FORWARD, NULL);
    TEST_ASSERT_TRUE(motor_cmd_ok(r));
    motor_executor_tick(s_exec);
    TEST_ASSERT_EQUAL_INT(MOTOR_STATE_RUNNING, motor_exec_state(hal, 0));

    s_fx.port_status = MOTOR_PORT_FATAL;
    motor_executor_tick(s_exec);
    TEST_ASSERT_EQUAL_INT(MOTOR_STATE_FAULT, motor_exec_state(hal, 0));
    TEST_ASSERT_EQUAL_INT(MOTOR_FAULT_DRIVER_PORT_FATAL, motor_exec_fault_code(hal, 0));

    r = motor_exec_run(hal, 0, motor_speed_gear(1), MOTOR_DIR_FORWARD, NULL);
    TEST_ASSERT_FALSE(motor_cmd_ok(r));
    TEST_ASSERT_EQUAL_INT(MOTOR_REJECT_FATAL, r.reject);
    TEST_ASSERT_EQUAL_INT(MOTOR_STATE_FAULT, motor_exec_state(hal, 0));

    r = motor_exec_recover(hal, 0, MOTOR_RECOVERY_DRIVER_RESET);
    TEST_ASSERT_FALSE(motor_cmd_ok(r));
    TEST_ASSERT_EQUAL_INT(MOTOR_REJECT_FATAL, r.reject);

    init_executor();
    hal = s_exec;
    safety_output_hold_request();
    motor_executor_tick(s_exec);
    TEST_ASSERT_EQUAL_INT(MOTOR_STATE_ESTOP, motor_exec_state(hal, 0));
    r = motor_exec_run(hal, 0, motor_speed_gear(1), MOTOR_DIR_FORWARD, NULL);
    TEST_ASSERT_FALSE(motor_cmd_ok(r));
    TEST_ASSERT_EQUAL_INT(MOTOR_REJECT_SAFETY, r.reject);
    TEST_ASSERT_EQUAL_INT(MOTOR_STATE_ESTOP, motor_exec_state(hal, 0));
}

/** @brief hold 覆盖可续动 FAULT 后解除，须回到 FAULT 再经内清才能启动。 */
static void test_hold_over_fault_restores_fault_latch(void)
{
    motor_cmd_result_t r;
    motor_exec_t      *hal;
    int                resets;

    enable_overcurrent_monitor();
    rebind_executor();
    hal = s_exec;
    run_until_overcurrent(hal);
    resets = s_fx.reset_count;

    safety_output_hold_request();
    motor_executor_tick(s_exec);
    TEST_ASSERT_EQUAL_INT(MOTOR_STATE_ESTOP, motor_exec_state(hal, 0));
    TEST_ASSERT_EQUAL_INT(MOTOR_FAULT_OVERCURRENT, motor_exec_fault_code(hal, 0));

    TEST_ASSERT_EQUAL_INT(SW_OK, safety_output_hold_release());
    motor_executor_tick(s_exec);
    TEST_ASSERT_EQUAL_INT(MOTOR_STATE_FAULT, motor_exec_state(hal, 0));
    TEST_ASSERT_EQUAL_INT(MOTOR_FAULT_OVERCURRENT, motor_exec_fault_code(hal, 0));

    s_fx.current = 0;
    r            = motor_exec_run(hal, 0, motor_speed_gear(1), MOTOR_DIR_FORWARD, NULL);
    TEST_ASSERT_TRUE(motor_cmd_ok(r));
    TEST_ASSERT_TRUE(s_fx.reset_count > resets);
    TEST_ASSERT_EQUAL_INT(MOTOR_FAULT_NONE, motor_exec_fault_code(hal, 0));
    TEST_ASSERT_NOT_EQUAL(MOTOR_STATE_FAULT, motor_exec_state(hal, 0));
}

/** @brief 未定义故障码位导致 bind 失败，槽位不进入可 tick 状态。 */
static void test_confirm_faults_undefined_bit_bind_fails(void)
{
    motor_init_result_t ir;
    motor_exec_t       *exec = NULL;

    s_cfg.motors[0].confirm_faults = 1u << 31;
    motor_executor_test_reset();
    safety_output_hold_reset();
    ir = motor_executor_bind(0U, &s_cfg, &s_ports, &exec);
    TEST_ASSERT_FALSE(ir.ok);
    TEST_ASSERT_EQUAL_STRING("confirmFaults has undefined bits", ir.error);
    TEST_ASSERT_NULL(exec);
}

int main(void)
{
    UNITY_BEGIN();

    WDF_RUN_TEST(test_run_continuous_and_state_via_port, "", "验证经端口连续运行与状态查询");
    WDF_RUN_TEST(test_slot_binding_is_fixed_and_reinit_keeps_handle, "", "验证槽位只绑定一次且重初始化保持句柄");
    WDF_RUN_TEST(test_move_to_time_and_stop_via_port, "", "验证经端口按时到位与停止");
    WDF_RUN_TEST(test_run_updates_speed_without_restart, "", "验证运行中再次 run 只更新目标不重启");
    WDF_RUN_TEST(test_run_updates_pending_while_waiting_start, "", "验证冷却排队中再次 run 更新挂起目标");
    WDF_RUN_TEST(test_recover_via_port, "", "验证经端口故障恢复");
    WDF_RUN_TEST(test_query_helpers_via_port, "", "验证经端口位置与基准/编码器查询");
    WDF_RUN_TEST(test_incremental_idle_skips_encoder_raw, "", "验证增量轴空闲不读编码器");
    WDF_RUN_TEST(test_query_helpers_return_safe_defaults_for_bad_motor, "", "验证越界电机查询返回安全默认值");
    WDF_RUN_TEST(test_run_rejects_unset_dir, "", "验证未指定方向的运动命令被拒绝");
    WDF_RUN_TEST(test_output_hold_cuts_and_rejects_until_release, "SAFE-14", "验证输出抑制切断电机并拒绝运动直到释放");
    WDF_RUN_TEST(test_output_hold_reset_rejected_while_di_active, "SAFE-15", "验证急停 DI 有效时拒绝释放输出抑制");
    WDF_RUN_TEST(test_pop_event_via_port, "", "验证经端口取出运动事件");
    WDF_RUN_TEST(test_timeout_event_copies_move_spec, "", "验证超时事件携带本次运动限位掩码与到位方式");
    WDF_RUN_TEST(test_overcurrent_event_copies_current, "", "验证过流事件携带采样电流与阈值");
    WDF_RUN_TEST(test_pop_event_for_keeps_other_motors, "", "验证按电机取事件保留其它电机");
    WDF_RUN_TEST(test_event_queue_prefers_drop_same_motor, "", "验证队列满时优先丢同电机事件");
    WDF_RUN_TEST(test_request_stop_waits_until_driver_idle, "", "验证受控停止等到功率级停下再切断");
    WDF_RUN_TEST(test_request_stop_timeout_force_cutoff, "", "验证受控停止超时后强制切断");
    WDF_RUN_TEST(test_repeat_stop_does_not_restart_timeout, "", "验证停止中重复 stop 不重置超时");
    WDF_RUN_TEST(test_request_stop_failure_cuts_off, "", "验证 request_stop 失败则立即切断");
    WDF_RUN_TEST(test_run_during_stopping_starts_after_halt, "", "验证停止中再次 run 等停稳后再启动");
    WDF_RUN_TEST(test_running_poll_busy_then_failed_enters_prepare_failed,
                 "",
                 "验证运行中 poll BUSY 忽略、FAILED 进入 PREPARE_FAILED");
    WDF_RUN_TEST(test_resumable_overcurrent_run_without_recover, "", "验证默认可续动过流后不经 recover 的 run 可受理");
    WDF_RUN_TEST(
        test_overtemp_confirm_rejects_until_recover, "", "验证仅过温需确认：过流可续动、过温拒令、recover 后可再 run");
    WDF_RUN_TEST(test_resumable_reset_fail_keeps_fault, "", "验证可续动复位失败则 run 拒绝且 tick 不自复");
    WDF_RUN_TEST(test_resumable_reject_does_not_clear_fault, "", "验证方向或互锁拒绝时不得内清可续动 FAULT");
    WDF_RUN_TEST(test_fatal_and_hold_not_resumable, "", "验证 fatal 与 hold/ESTOP 在位图为 0 时仍拒令");
    WDF_RUN_TEST(test_hold_over_fault_restores_fault_latch, "", "验证 hold 覆盖 FAULT 解除后恢复故障门闩并内清");
    WDF_RUN_TEST(test_confirm_faults_undefined_bit_bind_fails, "", "验证未定义故障码位导致 bind 失败");

    return UNITY_END();
}
