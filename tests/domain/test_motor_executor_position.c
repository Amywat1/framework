/**
 * @file test_motor_executor_position.c
 * @brief 电机执行器挡位位置控制单元测试
 */

#include "domain/mechanism/motor/motor_executor.h"
#include "domain/ports/outbound/safety/safety_output_hold.h"
#include "wdf_test_spec.h"

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

typedef struct {
    uint64_t                now_ms;
    int64_t                 position;
    int                     current;
    int                     output_count;
    int                     cutoff_count;
    int                     zero_count;
    bool                    zero_succeeds;
    bool                    origin_active;
    bool                    pos_limit_active;
    bool                    neg_limit_active;
    int                     arrived_count;
    int                     timeout_count;
    int                     fault_count;
    motor_end_condition_t   last_arrived_trigger;
    motor_limit_kind_t      last_arrived_limit;
    uint64_t                last_arrived_elapsed_ms;
    motor_exec_fault_code_t last_fault;
    motor_speed_t           last_speed;
    motor_dir_t             last_direction;
} position_fixture_t;

static position_fixture_t s_fixture;
static motor_exec_t      *s_executor;
static int                s_watchdog_ms;
static int                s_enc_stall_ticks;
static bool               s_monitor_current;
static motor_dir_t        s_home_dir;

static uint64_t clock_now(void *ctx)
{
    return ((position_fixture_t *)ctx)->now_ms;
}

static sw_err_t driver_set_output(void *ctx, motor_speed_t speed, motor_dir_t direction)
{
    position_fixture_t *fixture = (position_fixture_t *)ctx;

    fixture->output_count++;
    fixture->last_speed     = speed;
    fixture->last_direction = direction;
    return SW_OK;
}

static sw_err_t driver_cutoff(void *ctx)
{
    ((position_fixture_t *)ctx)->cutoff_count++;
    return SW_OK;
}

static bool driver_reset(void *ctx)
{
    (void)ctx;
    return true;
}

static bool driver_is_running(void *ctx)
{
    (void)ctx;
    return true;
}

static int driver_current(void *ctx)
{
    return ((position_fixture_t *)ctx)->current;
}

static int64_t encoder_raw(void *ctx)
{
    return ((position_fixture_t *)ctx)->position;
}

static bool encoder_zero(void *ctx)
{
    position_fixture_t *fixture = (position_fixture_t *)ctx;

    fixture->zero_count++;
    if (fixture->zero_succeeds) {
        fixture->position = 0;
        return true;
    }
    return false;
}

static bool sensor_limit(void *ctx, int motor, motor_limit_kind_t kind)
{
    position_fixture_t *fixture = (position_fixture_t *)ctx;

    (void)motor;
    if (kind == MOTOR_LIMIT_ORIGIN) {
        return fixture->origin_active;
    }
    if (kind == MOTOR_LIMIT_POS) {
        return fixture->pos_limit_active;
    }
    if (kind == MOTOR_LIMIT_NEG) {
        return fixture->neg_limit_active;
    }
    return false;
}

static void capture_event(const motor_event_t *event, void *ctx)
{
    position_fixture_t *fixture = (position_fixture_t *)ctx;

    if (event->type == MOTOR_EVENT_ARRIVED) {
        fixture->arrived_count++;
        fixture->last_arrived_trigger    = event->trigger;
        fixture->last_arrived_limit      = event->limit;
        fixture->last_arrived_elapsed_ms = event->elapsed_ms;
    } else if (event->type == MOTOR_EVENT_TIMEOUT) {
        fixture->timeout_count++;
    } else if (event->type == MOTOR_EVENT_FAULT) {
        fixture->fault_count++;
        fixture->last_fault = event->fault;
    }
}

static void tick_at(int64_t position)
{
    motor_event_t ev;

    s_fixture.position = position;
    s_fixture.now_ms += 10U;
    motor_executor_tick(s_executor);
    while (motor_exec_pop_event(s_executor, &ev)) {
        capture_event(&ev, &s_fixture);
    }
}

static void init_executor(int64_t initial_position, motor_encoder_kind_t encoder_kind)
{
    static motor_driver_t   driver;
    static motor_encoder_t  encoder;
    static motor_driver_t  *drivers[1];
    static motor_encoder_t *encoders[1];
    static motor_clock_t    clock;
    static motor_sensors_t  sensors;
    motor_config_t          config;
    motor_ports_t           ports;
    motor_init_result_t     result;

    memset(&s_fixture, 0, sizeof(s_fixture));
    memset(&config, 0, sizeof(config));
    motor_executor_test_reset();
    safety_output_hold_reset();
    s_executor              = NULL;
    s_fixture.position      = initial_position;
    s_fixture.zero_succeeds = true;

    driver = (motor_driver_t){
        .set_output = driver_set_output,
        .cutoff     = driver_cutoff,
        .reset      = driver_reset,
        .is_running = driver_is_running,
        .current    = driver_current,
        .ctx        = &s_fixture,
    };
    encoder = (motor_encoder_t){
        .raw  = encoder_raw,
        .zero = encoder_zero,
        .ctx  = &s_fixture,
    };
    clock       = (motor_clock_t){clock_now, &s_fixture};
    sensors     = (motor_sensors_t){sensor_limit, &s_fixture};
    drivers[0]  = &driver;
    encoders[0] = &encoder;
    ports       = (motor_ports_t){
              .clock    = &clock,
              .drivers  = drivers,
              .encoders = encoders,
              .sensors  = &sensors,
    };
    config.motor_count                   = 1;
    config.driver_count                  = 1;
    config.tick_ms                       = 10;
    config.watchdog_ms                   = s_watchdog_ms;
    config.motors[0].driver_index        = 0;
    config.motors[0].has_encoder         = true;
    config.motors[0].encoder_kind        = encoder_kind;
    config.motors[0].pos_tolerance       = 5;
    config.motors[0].decel_point         = 20;
    config.motors[0].position_slow_gear  = 1;
    config.motors[0].default_max_time_ms = 1000;
    config.motors[0].gear_count          = 2;
    config.motors[0].home_dir            = s_home_dir;
    config.motors[0].enc_stall_ticks     = s_enc_stall_ticks;
    if (s_monitor_current) {
        config.motors[0].mon.monitor_current  = true;
        config.motors[0].mon.startup_delay_ms = 0;
        config.motors[0].mon.cur_max_accel    = 500;
        config.motors[0].mon.cur_max_steady   = 500;
        config.motors[0].mon.cur_min_accel    = 0;
        config.motors[0].mon.cur_min_steady   = 0;
        config.motors[0].mon.cur_confirm_ms   = 20;
        config.motors[0].accel_ms             = 0;
    }

    result = motor_executor_bind(0U, &config, &ports, &s_executor);
    TEST_ASSERT_TRUE_MESSAGE(result.ok, result.error);
    s_fixture.cutoff_count = 0;
}

void setUp(void)
{
    s_watchdog_ms     = 100;
    s_enc_stall_ticks = 0;
    s_monitor_current = false;
    s_home_dir        = MOTOR_HOME_DEFAULT_DIR;
}

void tearDown(void)
{
}

static void test_forward_position_move_slows_once_and_stops_after_overshoot(void)
{
    motor_move_spec_t  spec = {0};
    motor_cmd_result_t result;

    init_executor(0, MOTOR_ENC_ABSOLUTE);
    spec.use_position = true;
    spec.target_pos   = 100;
    result            = motor_exec_run(s_executor, 0, motor_speed_gear(2), MOTOR_DIR_FORWARD, &spec);
    TEST_ASSERT_TRUE(motor_cmd_ok(result));

    tick_at(0);
    TEST_ASSERT_EQUAL_INT(1, s_fixture.output_count);
    TEST_ASSERT_EQUAL_INT(MOTOR_SPEED_GEAR, s_fixture.last_speed.kind);
    TEST_ASSERT_EQUAL_INT(2, s_fixture.last_speed.value);

    tick_at(80);
    TEST_ASSERT_EQUAL_INT(2, s_fixture.output_count);
    TEST_ASSERT_EQUAL_INT(1, s_fixture.last_speed.value);
    tick_at(80);
    TEST_ASSERT_EQUAL_INT(2, s_fixture.output_count);

    tick_at(110);
    TEST_ASSERT_EQUAL_INT(MOTOR_PHASE_STOPPED, motor_exec_phase(s_executor, 0));
    TEST_ASSERT_EQUAL_INT(1, s_fixture.cutoff_count);
    TEST_ASSERT_EQUAL_INT(2, s_fixture.output_count);
}

static void test_reverse_position_move_stops_after_overshoot(void)
{
    motor_move_spec_t  spec = {0};
    motor_cmd_result_t result;

    init_executor(200, MOTOR_ENC_ABSOLUTE);
    spec.use_position = true;
    spec.target_pos   = 100;
    result            = motor_exec_run(s_executor, 0, motor_speed_gear(2), MOTOR_DIR_REVERSE, &spec);
    TEST_ASSERT_TRUE(motor_cmd_ok(result));

    tick_at(200);
    tick_at(120);
    TEST_ASSERT_EQUAL_INT(MOTOR_SPEED_GEAR, s_fixture.last_speed.kind);
    TEST_ASSERT_EQUAL_INT(1, s_fixture.last_speed.value);
    tick_at(80);

    TEST_ASSERT_EQUAL_INT(MOTOR_PHASE_STOPPED, motor_exec_phase(s_executor, 0));
    TEST_ASSERT_EQUAL_INT(1, s_fixture.cutoff_count);
    TEST_ASSERT_EQUAL_INT(2, s_fixture.output_count);
}

static void test_active_position_target_update_keeps_start_time_and_output(void)
{
    motor_move_spec_t  spec = {0};
    motor_cmd_result_t result;

    s_watchdog_ms = 2000;
    init_executor(0, MOTOR_ENC_ABSOLUTE);
    spec.use_position = true;
    spec.target_pos   = 100;
    result            = motor_exec_run(s_executor, 0, motor_speed_gear(1), MOTOR_DIR_FORWARD, &spec);
    TEST_ASSERT_TRUE(motor_cmd_ok(result));
    tick_at(0);
    TEST_ASSERT_EQUAL_INT(1, s_fixture.output_count);

    s_fixture.now_ms = 989U;
    spec.target_pos  = 150;
    result           = motor_exec_run(s_executor, 0, motor_speed_gear(1), MOTOR_DIR_FORWARD, &spec);
    TEST_ASSERT_TRUE(motor_cmd_ok(result));
    tick_at(20);

    TEST_ASSERT_EQUAL_INT(1, s_fixture.output_count);
    TEST_ASSERT_EQUAL_INT(MOTOR_PHASE_RUNNING, motor_exec_phase(s_executor, 0));

    tick_at(20);
    TEST_ASSERT_EQUAL_INT(MOTOR_PHASE_STOPPED, motor_exec_phase(s_executor, 0));
    TEST_ASSERT_EQUAL_INT(1, s_fixture.timeout_count);
}

static void test_origin_move_clears_hardware_and_software_once(void)
{
    motor_move_spec_t  spec = {0};
    motor_cmd_result_t result;

    init_executor(40, MOTOR_ENC_INCREMENTAL);
    spec.limit_mask = MOTOR_LIMIT_MASK_ORIGIN;
    result          = motor_exec_run(s_executor, 0, motor_speed_gear(1), MOTOR_DIR_REVERSE, &spec);
    TEST_ASSERT_TRUE(motor_cmd_ok(result));

    tick_at(45);
    s_fixture.origin_active = true;
    tick_at(50);

    TEST_ASSERT_EQUAL_INT(MOTOR_PHASE_STOPPED, motor_exec_phase(s_executor, 0));
    TEST_ASSERT_EQUAL_INT(1, s_fixture.zero_count);
    TEST_ASSERT_EQUAL_INT64(0, motor_exec_position(s_executor, 0));
    TEST_ASSERT_TRUE(motor_exec_baseline_trusted(s_executor, 0));
    TEST_ASSERT_EQUAL_INT(1, s_fixture.arrived_count);
    TEST_ASSERT_EQUAL_INT(0, s_fixture.fault_count);
    /* ORIGIN 路径 settle 两次后仍须保留真实耗时（非 0）。
     * move_to 于 now=0 启动，两拍 tick 后 now=20 到位。 */
    TEST_ASSERT_EQUAL_UINT64(20U, s_fixture.last_arrived_elapsed_ms);
}

/**
 * @brief  ORIGIN 限位运动在压原点时被外部停止，仍重建基准（不依赖 motor_home）
 * @note   已压监视限位时 stop 按 ARRIVED(LIMIT) 收尾，与触限同拍语义一致。
 */
static void test_origin_external_stop_while_pressed_rebuilds_baseline(void)
{
    motor_move_spec_t  spec = {0};
    motor_cmd_result_t result;

    init_executor(40, MOTOR_ENC_INCREMENTAL);
    spec.limit_mask = MOTOR_LIMIT_MASK_ORIGIN;
    result          = motor_exec_run(s_executor, 0, motor_speed_gear(1), MOTOR_DIR_REVERSE, &spec);
    TEST_ASSERT_TRUE(motor_cmd_ok(result));

    tick_at(45);
    TEST_ASSERT_EQUAL_INT(MOTOR_PHASE_RUNNING, motor_exec_phase(s_executor, 0));

    s_fixture.origin_active = true;
    result                  = motor_exec_stop(s_executor, 0);
    TEST_ASSERT_TRUE(motor_cmd_ok(result));
    TEST_ASSERT_EQUAL_STRING("stopping", result.reason);
    tick_at(45);
    TEST_ASSERT_EQUAL_INT(MOTOR_PHASE_STOPPED, motor_exec_phase(s_executor, 0));
    TEST_ASSERT_EQUAL_INT(1, s_fixture.arrived_count);
    TEST_ASSERT_EQUAL_INT(MOTOR_END_LIMIT, s_fixture.last_arrived_trigger);
    TEST_ASSERT_EQUAL_INT(MOTOR_LIMIT_ORIGIN, s_fixture.last_arrived_limit);
    TEST_ASSERT_EQUAL_INT(1, s_fixture.zero_count);
    TEST_ASSERT_EQUAL_INT64(0, motor_exec_position(s_executor, 0));
    TEST_ASSERT_TRUE(motor_exec_baseline_trusted(s_executor, 0));
}

/**
 * @brief  已压 POS 限位时外部 stop 记为 ARRIVED，而非 STOPPED。
 */
static void test_stop_while_watched_pos_limit_arrives(void)
{
    motor_move_spec_t  spec = {0};
    motor_cmd_result_t result;

    init_executor(0, MOTOR_ENC_ABSOLUTE);
    spec.limit_mask  = MOTOR_LIMIT_MASK_POS;
    spec.max_time_ms = 5000;
    result           = motor_exec_run(s_executor, 0, motor_speed_gear(1), MOTOR_DIR_FORWARD, &spec);
    TEST_ASSERT_TRUE(motor_cmd_ok(result));

    tick_at(0);
    TEST_ASSERT_EQUAL_INT(MOTOR_PHASE_RUNNING, motor_exec_phase(s_executor, 0));

    s_fixture.pos_limit_active = true;
    result                     = motor_exec_stop(s_executor, 0);
    TEST_ASSERT_TRUE(motor_cmd_ok(result));
    TEST_ASSERT_EQUAL_STRING("stopping", result.reason);
    tick_at(0);
    TEST_ASSERT_EQUAL_INT(MOTOR_PHASE_STOPPED, motor_exec_phase(s_executor, 0));
    TEST_ASSERT_EQUAL_INT(1, s_fixture.arrived_count);
    TEST_ASSERT_EQUAL_INT(MOTOR_END_LIMIT, s_fixture.last_arrived_trigger);
    TEST_ASSERT_EQUAL_INT(MOTOR_LIMIT_POS, s_fixture.last_arrived_limit);
}

/**
 * @brief  编码器停滞后位置运动被拒，限位运动与归位仍放行
 * @note   降级的目的是不再依据不可信的位置动作，同时保留设备自行走回原点的能力：
 *         若一并拒绝限位运动与归位，机器就只能等人现场处理。
 */
static void test_unhealthy_encoder_rejects_position_move_but_allows_homing(void)
{
    motor_move_spec_t  spec = {0};
    motor_cmd_result_t result;
    int                i;

    s_enc_stall_ticks = 3;
    init_executor(40, MOTOR_ENC_INCREMENTAL);
    /* 连续 3 拍无脉冲即判定停滞；夹具默认不配置该阈值，检测不启用。 */

    /* 先归位建立可信基准，否则位置运动会先被 baseline-untrusted 拦下，
     * 无法区分是基准未建立还是编码器不健康。限位须在运动开始后才生效。 */
    result = motor_exec_home(s_executor, 0);
    TEST_ASSERT_TRUE(motor_cmd_ok(result));
    tick_at(45);
    s_fixture.origin_active = true;
    tick_at(50);
    TEST_ASSERT_TRUE(motor_exec_baseline_trusted(s_executor, 0));
    TEST_ASSERT_TRUE(motor_exec_encoder_healthy(s_executor, 0));

    /* 运动中冻结脉冲：位置读数不再变化，累计到阈值后判定编码器不健康。 */
    s_fixture.origin_active = false;
    spec.use_position       = true;
    spec.target_pos         = 500;
    result                  = motor_exec_run(s_executor, 0, motor_speed_gear(1), MOTOR_DIR_FORWARD, &spec);
    TEST_ASSERT_TRUE(motor_cmd_ok(result));
    for (i = 0; i < 5; ++i) {
        tick_at(50);
    }
    TEST_ASSERT_FALSE(motor_exec_encoder_healthy(s_executor, 0));

    /* 位置运动被拒，理由是编码器不健康而非基准未建立。 */
    result = motor_exec_run(s_executor, 0, motor_speed_gear(1), MOTOR_DIR_FORWARD, &spec);
    TEST_ASSERT_FALSE(motor_cmd_ok(result));

    /* 归位仍可下发，机器据此自行恢复。归位要求电机已停，先停当前运动。 */
    TEST_ASSERT_TRUE(motor_cmd_ok(motor_exec_stop(s_executor, 0)));
    tick_at(50);
    result = motor_exec_home(s_executor, 0);
    TEST_ASSERT_TRUE(motor_cmd_ok(result));
}

/**
 * @brief  归位重建基准后编码器恢复健康
 */
static void test_homing_restores_encoder_health(void)
{
    motor_move_spec_t spec = {0};
    int               i;

    s_enc_stall_ticks = 3;
    init_executor(40, MOTOR_ENC_INCREMENTAL);

    spec.use_position = true;
    spec.target_pos   = 500;
    TEST_ASSERT_TRUE(motor_cmd_ok(motor_exec_home(s_executor, 0)));
    tick_at(45);
    s_fixture.origin_active = true;
    tick_at(50);
    TEST_ASSERT_TRUE(motor_exec_encoder_healthy(s_executor, 0));

    /* 冻结脉冲使编码器判定为不健康。 */
    s_fixture.origin_active = false;
    TEST_ASSERT_TRUE(motor_cmd_ok(motor_exec_run(s_executor, 0, motor_speed_gear(1), MOTOR_DIR_FORWARD, &spec)));
    for (i = 0; i < 5; ++i) {
        tick_at(50);
    }
    TEST_ASSERT_FALSE(motor_exec_encoder_healthy(s_executor, 0));

    /* 归位撞上原点限位：清零基准的同一路径恢复健康。归位要求电机已停。 */
    TEST_ASSERT_TRUE(motor_cmd_ok(motor_exec_stop(s_executor, 0)));
    tick_at(50);
    TEST_ASSERT_TRUE(motor_cmd_ok(motor_exec_home(s_executor, 0)));
    tick_at(55);
    s_fixture.origin_active = true;
    tick_at(60);
    TEST_ASSERT_TRUE(motor_exec_encoder_healthy(s_executor, 0));
    TEST_ASSERT_TRUE(motor_exec_baseline_trusted(s_executor, 0));
}

static void test_origin_clear_failure_keeps_baseline_untrusted_and_faults(void)
{
    motor_move_spec_t  spec = {0};
    motor_cmd_result_t result;

    init_executor(40, MOTOR_ENC_INCREMENTAL);
    s_fixture.zero_succeeds = false;
    spec.limit_mask         = MOTOR_LIMIT_MASK_ORIGIN;
    result                  = motor_exec_run(s_executor, 0, motor_speed_gear(1), MOTOR_DIR_REVERSE, &spec);
    TEST_ASSERT_TRUE(motor_cmd_ok(result));

    tick_at(45);
    s_fixture.origin_active = true;
    tick_at(50);

    TEST_ASSERT_EQUAL_INT(MOTOR_ZERO_MAX_TRIES, s_fixture.zero_count);
    TEST_ASSERT_EQUAL_INT(MOTOR_PHASE_FAULT, motor_exec_phase(s_executor, 0));
    TEST_ASSERT_EQUAL_INT(MOTOR_FAULT_ENCODER_SIGNAL, motor_exec_fault_code(s_executor, 0));
    TEST_ASSERT_FALSE(motor_exec_baseline_trusted(s_executor, 0));
    TEST_ASSERT_NOT_EQUAL(0, motor_exec_position(s_executor, 0));
    TEST_ASSERT_EQUAL_INT(0, s_fixture.arrived_count);
    TEST_ASSERT_EQUAL_INT(1, s_fixture.fault_count);
}

/** @brief 仅电流停：持续超限 confirm_ms 后正常到位，不报故障。 */
static void test_current_stop_arrives_after_confirm(void)
{
    motor_move_spec_t  spec = {0};
    motor_cmd_result_t result;

    init_executor(0, MOTOR_ENC_ABSOLUTE);
    spec.use_current        = true;
    spec.current_limit      = 100;
    spec.current_confirm_ms = 30; /* 3 拍 */
    spec.current_blank_ms   = 0;
    spec.max_time_ms        = 5000;
    result                  = motor_exec_run(s_executor, 0, motor_speed_gear(1), MOTOR_DIR_FORWARD, &spec);
    TEST_ASSERT_TRUE(motor_cmd_ok(result));

    s_fixture.current = 50;
    tick_at(0);
    tick_at(0);
    TEST_ASSERT_EQUAL_INT(MOTOR_PHASE_RUNNING, motor_exec_phase(s_executor, 0));
    TEST_ASSERT_EQUAL_INT(0, s_fixture.arrived_count);

    s_fixture.current = 150;
    tick_at(0); /* 10 */
    TEST_ASSERT_EQUAL_INT(MOTOR_PHASE_RUNNING, motor_exec_phase(s_executor, 0));
    tick_at(0); /* 20 */
    TEST_ASSERT_EQUAL_INT(MOTOR_PHASE_RUNNING, motor_exec_phase(s_executor, 0));
    tick_at(0); /* 30 → 到位 */

    TEST_ASSERT_EQUAL_INT(MOTOR_PHASE_STOPPED, motor_exec_phase(s_executor, 0));
    TEST_ASSERT_EQUAL_INT(1, s_fixture.arrived_count);
    TEST_ASSERT_EQUAL_INT(MOTOR_END_CURRENT, s_fixture.last_arrived_trigger);
    TEST_ASSERT_EQUAL_INT(0, s_fixture.fault_count);
}

/** @brief 启动消隐期内超限不累计；消隐结束后再确认才电流停。 */
static void test_current_stop_respects_blank_ms(void)
{
    motor_move_spec_t  spec = {0};
    motor_cmd_result_t result;

    init_executor(0, MOTOR_ENC_ABSOLUTE);
    spec.use_current        = true;
    spec.current_limit      = 100;
    spec.current_confirm_ms = 20;
    spec.current_blank_ms   = 30;
    spec.max_time_ms        = 5000;
    result                  = motor_exec_run(s_executor, 0, motor_speed_gear(1), MOTOR_DIR_FORWARD, &spec);
    TEST_ASSERT_TRUE(motor_cmd_ok(result));

    s_fixture.current = 200;
    tick_at(0); /* 10 blank */
    tick_at(0); /* 20 blank */
    tick_at(0); /* 30 消隐结束，本拍累计=10 */
    TEST_ASSERT_EQUAL_INT(MOTOR_PHASE_RUNNING, motor_exec_phase(s_executor, 0));
    tick_at(0); /* 40 累计=20 → 到位 */

    TEST_ASSERT_EQUAL_INT(MOTOR_PHASE_STOPPED, motor_exec_phase(s_executor, 0));
    TEST_ASSERT_EQUAL_INT(1, s_fixture.arrived_count);
    TEST_ASSERT_EQUAL_INT(MOTOR_END_CURRENT, s_fixture.last_arrived_trigger);
    TEST_ASSERT_EQUAL_INT(0, s_fixture.fault_count);
}

/**
 * @brief 电流停与过流故障共存：阈值低于故障阈值时只电流停；
 *        高于故障阈值且 confirm 更短时走故障。
 */
static void test_current_stop_coexists_with_overcurrent_fault(void)
{
    motor_move_spec_t  spec = {0};
    motor_cmd_result_t result;

    s_monitor_current = true;
    init_executor(0, MOTOR_ENC_ABSOLUTE);

    spec.use_current        = true;
    spec.current_limit      = 100;
    spec.current_confirm_ms = 20;
    spec.current_blank_ms   = 0;
    spec.max_time_ms        = 5000;
    result                  = motor_exec_run(s_executor, 0, motor_speed_gear(1), MOTOR_DIR_FORWARD, &spec);
    TEST_ASSERT_TRUE(motor_cmd_ok(result));

    /* 介于电流停与故障阈值之间 → 正常电流停 */
    s_fixture.current = 200;
    tick_at(0);
    tick_at(0);
    TEST_ASSERT_EQUAL_INT(MOTOR_PHASE_STOPPED, motor_exec_phase(s_executor, 0));
    TEST_ASSERT_EQUAL_INT(1, s_fixture.arrived_count);
    TEST_ASSERT_EQUAL_INT(MOTOR_END_CURRENT, s_fixture.last_arrived_trigger);
    TEST_ASSERT_EQUAL_INT(0, s_fixture.fault_count);

    /* 再启一次，电流超过故障阈值；同拍 check_end 先于 monitor，仍电流停 */
    memset(&spec, 0, sizeof(spec));
    spec.use_current        = true;
    spec.current_limit      = 100;
    spec.current_confirm_ms = 20;
    spec.max_time_ms        = 5000;
    s_fixture.arrived_count = 0;
    s_fixture.fault_count   = 0;
    result                  = motor_exec_run(s_executor, 0, motor_speed_gear(1), MOTOR_DIR_FORWARD, &spec);
    TEST_ASSERT_TRUE(motor_cmd_ok(result));
    s_fixture.current = 600;
    tick_at(0);
    tick_at(0);
    TEST_ASSERT_EQUAL_INT(MOTOR_PHASE_STOPPED, motor_exec_phase(s_executor, 0));
    TEST_ASSERT_EQUAL_INT(1, s_fixture.arrived_count);
    TEST_ASSERT_EQUAL_INT(MOTOR_END_CURRENT, s_fixture.last_arrived_trigger);
    TEST_ASSERT_EQUAL_INT(0, s_fixture.fault_count);

    /* 关闭电流停，仅过流监测 → 故障 */
    memset(&spec, 0, sizeof(spec));
    spec.use_time           = true;
    spec.duration_ms        = 5000;
    spec.max_time_ms        = 5000;
    s_fixture.arrived_count = 0;
    s_fixture.fault_count   = 0;
    result                  = motor_exec_run(s_executor, 0, motor_speed_gear(1), MOTOR_DIR_FORWARD, &spec);
    TEST_ASSERT_TRUE(motor_cmd_ok(result));
    s_fixture.current = 600;
    tick_at(0);
    tick_at(0);
    TEST_ASSERT_EQUAL_INT(MOTOR_PHASE_FAULT, motor_exec_phase(s_executor, 0));
    TEST_ASSERT_EQUAL_INT(MOTOR_FAULT_OVERCURRENT, s_fixture.last_fault);
    TEST_ASSERT_EQUAL_INT(1, s_fixture.fault_count);
}

/** @brief 无编码器轴上电后 baseline_trusted 恒为 true。 */
static void test_no_encoder_baseline_trusted_at_init(void)
{
    static motor_driver_t   driver;
    static motor_driver_t  *drivers[1];
    static motor_encoder_t *encoders[1];
    static motor_clock_t    clock;
    static motor_sensors_t  sensors;
    motor_config_t          config;
    motor_ports_t           ports;
    motor_init_result_t     result;

    memset(&s_fixture, 0, sizeof(s_fixture));
    memset(&config, 0, sizeof(config));
    motor_executor_test_reset();
    safety_output_hold_reset();
    s_executor              = NULL;
    s_fixture.zero_succeeds = true;

    driver = (motor_driver_t){
        .set_output = driver_set_output,
        .cutoff     = driver_cutoff,
        .reset      = driver_reset,
        .is_running = driver_is_running,
        .current    = driver_current,
        .ctx        = &s_fixture,
    };
    clock       = (motor_clock_t){clock_now, &s_fixture};
    sensors     = (motor_sensors_t){sensor_limit, &s_fixture};
    drivers[0]  = &driver;
    encoders[0] = NULL;
    ports       = (motor_ports_t){
              .clock    = &clock,
              .drivers  = drivers,
              .encoders = encoders,
              .sensors  = &sensors,
    };
    config.motor_count                   = 1;
    config.driver_count                  = 1;
    config.tick_ms                       = 10;
    config.watchdog_ms                   = 100;
    config.motors[0].driver_index        = 0;
    config.motors[0].has_encoder         = false;
    config.motors[0].default_max_time_ms = 1000;
    config.motors[0].gear_count          = 2;
    config.motors[0].home_dir            = MOTOR_HOME_DEFAULT_DIR;

    result = motor_executor_bind(0U, &config, &ports, &s_executor);
    TEST_ASSERT_TRUE_MESSAGE(result.ok, result.error);
    TEST_ASSERT_TRUE(motor_exec_baseline_trusted(s_executor, 0));
    TEST_ASSERT_TRUE(motor_exec_encoder_healthy(s_executor, 0));
}

/** @brief POS|NEG 掩码：任一硬限位触发即停，并回报实际触发种类。 */
static void test_limit_mask_pos_or_neg_stops(void)
{
    motor_move_spec_t  spec = {0};
    motor_cmd_result_t result;

    init_executor(0, MOTOR_ENC_ABSOLUTE);
    spec.limit_mask  = (uint8_t)(MOTOR_LIMIT_MASK_POS | MOTOR_LIMIT_MASK_NEG);
    spec.max_time_ms = 5000;
    result           = motor_exec_run(s_executor, 0, motor_speed_gear(1), MOTOR_DIR_FORWARD, &spec);
    TEST_ASSERT_TRUE(motor_cmd_ok(result));

    tick_at(0);
    TEST_ASSERT_EQUAL_INT(MOTOR_PHASE_RUNNING, motor_exec_phase(s_executor, 0));

    s_fixture.neg_limit_active = true;
    tick_at(0);
    TEST_ASSERT_EQUAL_INT(MOTOR_PHASE_STOPPED, motor_exec_phase(s_executor, 0));
    TEST_ASSERT_EQUAL_INT(1, s_fixture.arrived_count);
    TEST_ASSERT_EQUAL_INT(MOTOR_END_LIMIT, s_fixture.last_arrived_trigger);
    TEST_ASSERT_EQUAL_INT(MOTOR_LIMIT_NEG, s_fixture.last_arrived_limit);
}

/** @brief 同拍 ORIGIN 与 POS 同时有效时优先回报 ORIGIN。 */
static void test_limit_mask_prefers_origin_when_multiple(void)
{
    motor_move_spec_t  spec = {0};
    motor_cmd_result_t result;

    init_executor(0, MOTOR_ENC_ABSOLUTE);
    spec.limit_mask  = MOTOR_LIMIT_MASK_ALL;
    spec.max_time_ms = 5000;
    result           = motor_exec_run(s_executor, 0, motor_speed_gear(1), MOTOR_DIR_FORWARD, &spec);
    TEST_ASSERT_TRUE(motor_cmd_ok(result));

    s_fixture.origin_active    = true;
    s_fixture.pos_limit_active = true;
    tick_at(0);
    TEST_ASSERT_EQUAL_INT(MOTOR_PHASE_STOPPED, motor_exec_phase(s_executor, 0));
    TEST_ASSERT_EQUAL_INT(MOTOR_END_LIMIT, s_fixture.last_arrived_trigger);
    TEST_ASSERT_EQUAL_INT(MOTOR_LIMIT_ORIGIN, s_fixture.last_arrived_limit);
}

static void test_home_unset_dir_rejected_at_bind(void)
{
    motor_config_t          config;
    motor_init_result_t     result;
    static motor_driver_t   driver;
    static motor_encoder_t  encoder;
    static motor_driver_t  *drivers[1];
    static motor_encoder_t *encoders[1];
    static motor_clock_t    clock;
    static motor_sensors_t  sensors;
    motor_ports_t           ports;

    init_executor(0, MOTOR_ENC_INCREMENTAL);
    memset(&config, 0, sizeof(config));
    driver = (motor_driver_t){
        .set_output = driver_set_output,
        .cutoff     = driver_cutoff,
        .reset      = driver_reset,
        .is_running = driver_is_running,
        .current    = driver_current,
        .ctx        = &s_fixture,
    };
    encoder     = (motor_encoder_t){.raw = encoder_raw, .zero = encoder_zero, .ctx = &s_fixture};
    clock       = (motor_clock_t){clock_now, &s_fixture};
    sensors     = (motor_sensors_t){sensor_limit, &s_fixture};
    drivers[0]  = &driver;
    encoders[0] = &encoder;
    ports       = (motor_ports_t){
              .clock    = &clock,
              .drivers  = drivers,
              .encoders = encoders,
              .sensors  = &sensors,
    };
    config.motor_count                   = 1;
    config.driver_count                  = 1;
    config.tick_ms                       = 10;
    config.watchdog_ms                   = 1000;
    config.motors[0].driver_index        = 0;
    config.motors[0].has_encoder         = true;
    config.motors[0].encoder_kind        = MOTOR_ENC_INCREMENTAL;
    config.motors[0].default_max_time_ms = 1000;
    config.motors[0].gear_count          = 2;
    motor_executor_test_reset();
    s_executor = NULL;
    result     = motor_executor_bind(0U, &config, &ports, &s_executor);
    TEST_ASSERT_FALSE(result.ok);
}

static void test_home_forward_dir_runs_forward(void)
{
    s_home_dir = MOTOR_DIR_FORWARD;
    init_executor(0, MOTOR_ENC_INCREMENTAL);
    TEST_ASSERT_TRUE(motor_cmd_ok(motor_exec_home(s_executor, 0)));
    tick_at(10);
    TEST_ASSERT_EQUAL_INT(MOTOR_DIR_FORWARD, s_fixture.last_direction);
}

static void test_motor_capacity_constants(void)
{
    TEST_ASSERT_EQUAL_INT(16, MOTOR_MAX_MOTORS);
    TEST_ASSERT_EQUAL_INT(16, MOTOR_MAX_DRIVERS);
    TEST_ASSERT_EQUAL_INT(16, MOTOR_MAX_INTERLOCKS);
    TEST_ASSERT_EQUAL_INT(4, MOTOR_EVENT_SLOT_CAP);
}

int main(void)
{
    UNITY_BEGIN();

    WDF_RUN_TEST(test_motor_capacity_constants, "", "验证电机执行器轴数容量常量");
    WDF_RUN_TEST(test_forward_position_move_slows_once_and_stops_after_overshoot,
                 "",
                 "验证正向位置移动仅减速一次并在越过目标后停止");
    WDF_RUN_TEST(test_reverse_position_move_stops_after_overshoot, "", "验证反向位置移动在越过目标后停止");
    WDF_RUN_TEST(test_active_position_target_update_keeps_start_time_and_output,
                 "",
                 "验证活动状态位置目标更新保持启动时间并输出");
    WDF_RUN_TEST(test_origin_move_clears_hardware_and_software_once, "", "验证原点移动仅清除一次软硬件位置");
    WDF_RUN_TEST(
        test_origin_external_stop_while_pressed_rebuilds_baseline, "", "验证压原点时外部停止按到位收尾并重建基准");
    WDF_RUN_TEST(test_stop_while_watched_pos_limit_arrives, "", "验证已压监视正限位时 stop 记为到位");
    WDF_RUN_TEST(test_no_encoder_baseline_trusted_at_init, "", "验证无编码器轴上电基准恒可信");
    WDF_RUN_TEST(
        test_origin_clear_failure_keeps_baseline_untrusted_and_faults, "", "验证原点清除失败保持基线不可信并进入故障");
    WDF_RUN_TEST(
        test_unhealthy_encoder_rejects_position_move_but_allows_homing, "", "验证不健康编码器拒绝位置移动但允许回零");
    WDF_RUN_TEST(test_homing_restores_encoder_health, "", "验证回零恢复编码器健康状态");
    WDF_RUN_TEST(test_home_unset_dir_rejected_at_bind, "", "验证回原方向未填时装载拒绝");
    WDF_RUN_TEST(test_home_forward_dir_runs_forward, "", "验证显式正向回原不被改写成反向");
    WDF_RUN_TEST(test_current_stop_arrives_after_confirm, "", "验证电流停经确认后正常到位");
    WDF_RUN_TEST(test_current_stop_respects_blank_ms, "", "验证电流停尊重启动消隐");
    WDF_RUN_TEST(test_current_stop_coexists_with_overcurrent_fault, "", "验证电流停与过流故障共存");
    WDF_RUN_TEST(test_limit_mask_pos_or_neg_stops, "", "验证硬限位掩码正负任一触发即停");
    WDF_RUN_TEST(test_limit_mask_prefers_origin_when_multiple, "", "验证硬限位同拍多路优先原点");
    return UNITY_END();
}
