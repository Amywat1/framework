/**
 * @file test_hal_motor_executor_position.c
 * @brief çµæºæ§è¡å¨æ¡ä½ä½ç½®æ§å¶ååæµè¯
 */

#include "adapters/outbound/hal/components/motor_exec/hal_motor_executor.h"
#include "wdf_test_spec.h"

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

typedef struct {
    uint64_t          now_ms;
    int64_t           position;
    int               current;
    int               output_count;
    int               cutoff_count;
    int               zero_count;
    bool              zero_succeeds;
    bool              origin_active;
    bool              pos_limit_active;
    bool              neg_limit_active;
    int               arrived_count;
    int               timeout_count;
    int               fault_count;
    motor_end_condition_t last_arrived_trigger;
    motor_limit_kind_t    last_arrived_limit;
    uint64_t              last_arrived_elapsed_ms;
    motor_fault_code_t    last_fault;
    motor_speed_t     last_speed;
    motor_direction_t last_direction;
} position_fixture_t;

static position_fixture_t s_fixture;
static motor_executor_t   s_executor;

static uint64_t clock_now(void *ctx)
{
    return ((position_fixture_t *)ctx)->now_ms;
}

static sw_err_t driver_set_output(void *ctx, motor_speed_t speed, motor_direction_t direction)
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

static bool estop_active(void *ctx)
{
    (void)ctx;
    return false;
}

static void tick_at(int64_t position)
{
    s_fixture.position = position;
    s_fixture.now_ms += 10U;
    motor_tick(&s_executor);
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

static void init_executor(int64_t initial_position, motor_encoder_kind_t encoder_kind)
{
    static motor_driver_t   driver;
    static motor_encoder_t  encoder;
    static motor_driver_t  *drivers[1];
    static motor_encoder_t *encoders[1];
    static motor_clock_t    clock;
    static motor_sensors_t  sensors;
    static motor_estop_t    estop;
    motor_config_t          config;
    motor_ports_t           ports;
    motor_init_result_t     result;

    memset(&s_fixture, 0, sizeof(s_fixture));
    memset(&s_executor, 0, sizeof(s_executor));
    memset(&config, 0, sizeof(config));
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
    estop       = (motor_estop_t){estop_active, &s_fixture};
    drivers[0]  = &driver;
    encoders[0] = &encoder;
    ports       = (motor_ports_t){
              .clock    = &clock,
              .drivers  = drivers,
              .encoders = encoders,
              .sensors  = &sensors,
              .estop    = &estop,
    };
    config.motor_count                   = 1;
    config.driver_count                  = 1;
    config.tick_ms                       = 10;
    config.watchdog_ms                   = 100;
    config.motors[0].driver_index        = 0;
    config.motors[0].has_encoder         = true;
    config.motors[0].encoder_kind        = encoder_kind;
    config.motors[0].cap_position_move   = true;
    config.motors[0].pos_tolerance       = 5;
    config.motors[0].decel_point         = 20;
    config.motors[0].position_slow_gear  = 1;
    config.motors[0].default_max_move_ms = 1000;
    config.motors[0].gear_count          = 2;

    result = motor_init(&s_executor, &config, &ports);
    TEST_ASSERT_TRUE_MESSAGE(result.ok, result.error);
    motor_set_event_callback(&s_executor, capture_event, &s_fixture);
    s_fixture.cutoff_count = 0;
}

void setUp(void)
{
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
    result            = motor_move_to(&s_executor, 0, motor_speed_gear(2), MOTOR_DIR_FORWARD, &spec);
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
    TEST_ASSERT_EQUAL_INT(MOTOR_PHASE_STOPPED, motor_phase(&s_executor, 0));
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
    result            = motor_move_to(&s_executor, 0, motor_speed_gear(2), MOTOR_DIR_REVERSE, &spec);
    TEST_ASSERT_TRUE(motor_cmd_ok(result));

    tick_at(200);
    tick_at(120);
    TEST_ASSERT_EQUAL_INT(MOTOR_SPEED_GEAR, s_fixture.last_speed.kind);
    TEST_ASSERT_EQUAL_INT(1, s_fixture.last_speed.value);
    tick_at(80);

    TEST_ASSERT_EQUAL_INT(MOTOR_PHASE_STOPPED, motor_phase(&s_executor, 0));
    TEST_ASSERT_EQUAL_INT(1, s_fixture.cutoff_count);
    TEST_ASSERT_EQUAL_INT(2, s_fixture.output_count);
}

static void test_active_position_target_update_keeps_start_time_and_output(void)
{
    motor_move_spec_t  spec = {0};
    motor_cmd_result_t result;

    init_executor(0, MOTOR_ENC_ABSOLUTE);
    spec.use_position = true;
    spec.target_pos   = 100;
    result            = motor_move_to(&s_executor, 0, motor_speed_gear(1), MOTOR_DIR_FORWARD, &spec);
    TEST_ASSERT_TRUE(motor_cmd_ok(result));
    tick_at(0);
    TEST_ASSERT_EQUAL_INT(1, s_fixture.output_count);

    s_executor.cfg.watchdog_ms = 2000;
    s_fixture.now_ms           = 989U;
    spec.target_pos            = 150;
    result                     = motor_move_to(&s_executor, 0, motor_speed_gear(1), MOTOR_DIR_FORWARD, &spec);
    TEST_ASSERT_TRUE(motor_cmd_ok(result));
    tick_at(20);

    TEST_ASSERT_EQUAL_INT(1, s_fixture.output_count);
    TEST_ASSERT_EQUAL_INT(MOTOR_PHASE_RUNNING, motor_phase(&s_executor, 0));

    tick_at(20);
    TEST_ASSERT_EQUAL_INT(MOTOR_PHASE_STOPPED, motor_phase(&s_executor, 0));
    TEST_ASSERT_EQUAL_INT(1, s_fixture.timeout_count);
}

static void test_origin_move_clears_hardware_and_software_once(void)
{
    motor_move_spec_t  spec = {0};
    motor_cmd_result_t result;

    init_executor(40, MOTOR_ENC_INCREMENTAL);
    spec.limit_mask = MOTOR_LIMIT_MASK_ORIGIN;
    result         = motor_move_to(&s_executor, 0, motor_speed_gear(1), MOTOR_DIR_REVERSE, &spec);
    TEST_ASSERT_TRUE(motor_cmd_ok(result));

    tick_at(45);
    s_fixture.origin_active = true;
    tick_at(50);

    TEST_ASSERT_EQUAL_INT(MOTOR_PHASE_STOPPED, motor_phase(&s_executor, 0));
    TEST_ASSERT_EQUAL_INT(1, s_fixture.zero_count);
    TEST_ASSERT_EQUAL_INT64(0, motor_position(&s_executor, 0));
    TEST_ASSERT_TRUE(motor_baseline_trusted(&s_executor, 0));
    TEST_ASSERT_EQUAL_INT(1, s_fixture.arrived_count);
    TEST_ASSERT_EQUAL_INT(0, s_fixture.fault_count);
    /* ORIGIN 路径 settle 两次后仍须保留真实耗时（非 0）。
     * move_to 于 now=0 启动，两拍 tick 后 now=20 到位。 */
    TEST_ASSERT_EQUAL_UINT64(20U, s_fixture.last_arrived_elapsed_ms);
}

/**
 * @brief  ORIGIN éä½è¿å¨å¨ååç¹æ¶è¢«å¤é¨åæ­¢ï¼ä»éå»ºåºåï¼ä¸ä¾èµ motor_homeï¼
 */
static void test_origin_external_stop_while_pressed_rebuilds_baseline(void)
{
    motor_move_spec_t  spec = {0};
    motor_cmd_result_t result;

    init_executor(40, MOTOR_ENC_INCREMENTAL);
    spec.limit_mask = MOTOR_LIMIT_MASK_ORIGIN;
    result         = motor_move_to(&s_executor, 0, motor_speed_gear(1), MOTOR_DIR_REVERSE, &spec);
    TEST_ASSERT_TRUE(motor_cmd_ok(result));

    tick_at(45);
    TEST_ASSERT_EQUAL_INT(MOTOR_PHASE_RUNNING, motor_phase(&s_executor, 0));

    s_fixture.origin_active = true;
    TEST_ASSERT_TRUE(motor_cmd_ok(motor_stop(&s_executor, 0)));
    TEST_ASSERT_EQUAL_INT(MOTOR_PHASE_DECELERATING, motor_phase(&s_executor, 0));
    tick_at(50);

    TEST_ASSERT_EQUAL_INT(MOTOR_PHASE_STOPPED, motor_phase(&s_executor, 0));
    TEST_ASSERT_EQUAL_INT(1, s_fixture.zero_count);
    TEST_ASSERT_EQUAL_INT64(0, motor_position(&s_executor, 0));
    TEST_ASSERT_TRUE(motor_baseline_trusted(&s_executor, 0));
}

/**
 * @brief  ç¼ç å¨åæ»åä½ç½®è¿å¨è¢«æï¼éä½è¿å¨ä¸å½ä½ä»æ¾è¡
 * @note   éçº§çç®çæ¯ä¸åä¾æ®ä¸å¯ä¿¡çä½ç½®å¨ä½ï¼åæ¶ä¿çè®¾å¤èªè¡èµ°ååç¹çè½åï¼
 *         è¥ä¸å¹¶æç»éä½è¿å¨ä¸å½ä½ï¼æºå¨å°±åªè½ç­äººç°åºå¤çã
 */
static void test_unhealthy_encoder_rejects_position_move_but_allows_homing(void)
{
    motor_move_spec_t  spec = {0};
    motor_cmd_result_t result;
    int                i;

    init_executor(40, MOTOR_ENC_INCREMENTAL);
    /* è¿ç»­ 3 ææ èå²å³å¤å®åæ»ï¼å¤¹å·é»è®¤ä¸éç½®è¯¥éå¼ï¼æ£æµä¸å¯ç¨ã */
    s_executor.cfg.motors[0].enc_stall_ticks = 3;

    /* åå½ä½å»ºç«å¯ä¿¡åºåï¼å¦åä½ç½®è¿å¨ä¼åè¢« baseline-untrusted æ¦ä¸ï¼
     * æ æ³åºåæ¯åºåæªå»ºç«è¿æ¯ç¼ç å¨ä¸å¥åº·ãéä½é¡»å¨è¿å¨å¼å§åæçæã */
    result = motor_home(&s_executor, 0);
    TEST_ASSERT_TRUE(motor_cmd_ok(result));
    tick_at(45);
    s_fixture.origin_active = true;
    tick_at(50);
    TEST_ASSERT_TRUE(motor_baseline_trusted(&s_executor, 0));
    TEST_ASSERT_TRUE(motor_encoder_healthy(&s_executor, 0));

    /* è¿å¨ä¸­å»ç»èå²ï¼ä½ç½®è¯»æ°ä¸åååï¼ç´¯è®¡å°éå¼åå¤å®ç¼ç å¨ä¸å¥åº·ã */
    s_fixture.origin_active = false;
    spec.use_position       = true;
    spec.target_pos         = 500;
    result                  = motor_move_to(&s_executor, 0, motor_speed_gear(1), MOTOR_DIR_FORWARD, &spec);
    TEST_ASSERT_TRUE(motor_cmd_ok(result));
    for (i = 0; i < 5; ++i) {
        tick_at(50);
    }
    TEST_ASSERT_FALSE(motor_encoder_healthy(&s_executor, 0));

    /* ä½ç½®è¿å¨è¢«æï¼çç±æ¯ç¼ç å¨ä¸å¥åº·èéåºåæªå»ºç«ã */
    result = motor_move_to(&s_executor, 0, motor_speed_gear(1), MOTOR_DIR_FORWARD, &spec);
    TEST_ASSERT_FALSE(motor_cmd_ok(result));

    /* å½ä½ä»å¯ä¸åï¼æºå¨æ®æ­¤èªè¡æ¢å¤ãå½ä½è¦æ±çµæºå·²åï¼ååå½åè¿å¨ã */
    TEST_ASSERT_TRUE(motor_cmd_ok(motor_stop(&s_executor, 0)));
    tick_at(50);
    result = motor_home(&s_executor, 0);
    TEST_ASSERT_TRUE(motor_cmd_ok(result));
}

/**
 * @brief  å½ä½éå»ºåºååç¼ç å¨æ¢å¤å¥åº·
 */
static void test_homing_restores_encoder_health(void)
{
    motor_move_spec_t spec = {0};
    int               i;

    init_executor(40, MOTOR_ENC_INCREMENTAL);
    s_executor.cfg.motors[0].enc_stall_ticks = 3;

    spec.use_position = true;
    spec.target_pos   = 500;
    TEST_ASSERT_TRUE(motor_cmd_ok(motor_home(&s_executor, 0)));
    tick_at(45);
    s_fixture.origin_active = true;
    tick_at(50);
    TEST_ASSERT_TRUE(motor_encoder_healthy(&s_executor, 0));

    /* å»ç»èå²ä½¿ç¼ç å¨å¤å®ä¸ºä¸å¥åº·ã */
    s_fixture.origin_active = false;
    TEST_ASSERT_TRUE(motor_cmd_ok(motor_move_to(&s_executor, 0, motor_speed_gear(1), MOTOR_DIR_FORWARD, &spec)));
    for (i = 0; i < 5; ++i) {
        tick_at(50);
    }
    TEST_ASSERT_FALSE(motor_encoder_healthy(&s_executor, 0));

    /* å½ä½æä¸åç¹éä½ï¼æ¸é¶åºåçåä¸è·¯å¾æ¢å¤å¥åº·ãå½ä½è¦æ±çµæºå·²åã */
    TEST_ASSERT_TRUE(motor_cmd_ok(motor_stop(&s_executor, 0)));
    tick_at(50);
    TEST_ASSERT_TRUE(motor_cmd_ok(motor_home(&s_executor, 0)));
    tick_at(55);
    s_fixture.origin_active = true;
    tick_at(60);
    TEST_ASSERT_TRUE(motor_encoder_healthy(&s_executor, 0));
    TEST_ASSERT_TRUE(motor_baseline_trusted(&s_executor, 0));
}

static void test_origin_clear_failure_keeps_baseline_untrusted_and_faults(void)
{
    motor_move_spec_t  spec = {0};
    motor_cmd_result_t result;

    init_executor(40, MOTOR_ENC_INCREMENTAL);
    s_fixture.zero_succeeds = false;
    spec.limit_mask         = MOTOR_LIMIT_MASK_ORIGIN;
    result                  = motor_move_to(&s_executor, 0, motor_speed_gear(1), MOTOR_DIR_REVERSE, &spec);
    TEST_ASSERT_TRUE(motor_cmd_ok(result));

    tick_at(45);
    s_fixture.origin_active = true;
    tick_at(50);

    TEST_ASSERT_EQUAL_INT(MOTOR_ZERO_MAX_TRIES, s_fixture.zero_count);
    TEST_ASSERT_EQUAL_INT(MOTOR_PHASE_FAULT, motor_phase(&s_executor, 0));
    TEST_ASSERT_EQUAL_INT(MOTOR_FAULT_ENCODER_SIGNAL, motor_fault_code(&s_executor, 0));
    TEST_ASSERT_FALSE(motor_baseline_trusted(&s_executor, 0));
    TEST_ASSERT_NOT_EQUAL(0, motor_position(&s_executor, 0));
    TEST_ASSERT_EQUAL_INT(0, s_fixture.arrived_count);
    TEST_ASSERT_EQUAL_INT(1, s_fixture.fault_count);
}

/** @brief 仅电流停：持续超限 confirm_ms 后正常到位，不报故障。 */
static void test_current_stop_arrives_after_confirm(void)
{
    motor_move_spec_t  spec = {0};
    motor_cmd_result_t result;

    init_executor(0, MOTOR_ENC_ABSOLUTE);
    spec.use_current         = true;
    spec.current_limit       = 100;
    spec.current_confirm_ms  = 30; /* 3 拍 */
    spec.current_blank_ms    = 0;
    spec.max_time_ms         = 5000;
    result = motor_move_to(&s_executor, 0, motor_speed_gear(1), MOTOR_DIR_FORWARD, &spec);
    TEST_ASSERT_TRUE(motor_cmd_ok(result));

    s_fixture.current = 50;
    tick_at(0);
    tick_at(0);
    TEST_ASSERT_EQUAL_INT(MOTOR_PHASE_RUNNING, motor_phase(&s_executor, 0));
    TEST_ASSERT_EQUAL_INT(0, s_fixture.arrived_count);

    s_fixture.current = 150;
    tick_at(0); /* 10 */
    TEST_ASSERT_EQUAL_INT(MOTOR_PHASE_RUNNING, motor_phase(&s_executor, 0));
    tick_at(0); /* 20 */
    TEST_ASSERT_EQUAL_INT(MOTOR_PHASE_RUNNING, motor_phase(&s_executor, 0));
    tick_at(0); /* 30 → 到位 */

    TEST_ASSERT_EQUAL_INT(MOTOR_PHASE_STOPPED, motor_phase(&s_executor, 0));
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
    spec.use_current         = true;
    spec.current_limit       = 100;
    spec.current_confirm_ms  = 20;
    spec.current_blank_ms    = 30;
    spec.max_time_ms         = 5000;
    result = motor_move_to(&s_executor, 0, motor_speed_gear(1), MOTOR_DIR_FORWARD, &spec);
    TEST_ASSERT_TRUE(motor_cmd_ok(result));

    s_fixture.current = 200;
    tick_at(0); /* 10 blank */
    tick_at(0); /* 20 blank */
    tick_at(0); /* 30 消隐结束，本拍累计=10 */
    TEST_ASSERT_EQUAL_INT(MOTOR_PHASE_RUNNING, motor_phase(&s_executor, 0));
    tick_at(0); /* 40 累计=20 → 到位 */

    TEST_ASSERT_EQUAL_INT(MOTOR_PHASE_STOPPED, motor_phase(&s_executor, 0));
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

    init_executor(0, MOTOR_ENC_ABSOLUTE);
    s_executor.cfg.motors[0].mon.monitor_current  = true;
    s_executor.cfg.motors[0].mon.startup_delay_ms = 0;
    s_executor.cfg.motors[0].mon.cur_max_accel    = 500;
    s_executor.cfg.motors[0].mon.cur_max_steady   = 500;
    s_executor.cfg.motors[0].mon.cur_min_accel    = 0;
    s_executor.cfg.motors[0].mon.cur_min_steady   = 0;
    s_executor.cfg.motors[0].mon.cur_confirm_ms   = 20;
    s_executor.cfg.motors[0].accel_ms             = 0;

    spec.use_current         = true;
    spec.current_limit       = 100;
    spec.current_confirm_ms  = 20;
    spec.current_blank_ms    = 0;
    spec.max_time_ms         = 5000;
    result = motor_move_to(&s_executor, 0, motor_speed_gear(1), MOTOR_DIR_FORWARD, &spec);
    TEST_ASSERT_TRUE(motor_cmd_ok(result));

    /* 介于电流停与故障阈值之间 → 正常电流停 */
    s_fixture.current = 200;
    tick_at(0);
    tick_at(0);
    TEST_ASSERT_EQUAL_INT(MOTOR_PHASE_STOPPED, motor_phase(&s_executor, 0));
    TEST_ASSERT_EQUAL_INT(1, s_fixture.arrived_count);
    TEST_ASSERT_EQUAL_INT(MOTOR_END_CURRENT, s_fixture.last_arrived_trigger);
    TEST_ASSERT_EQUAL_INT(0, s_fixture.fault_count);

    /* 再启一次，电流超过故障阈值；同拍 check_end 先于 monitor，仍电流停 */
    memset(&spec, 0, sizeof(spec));
    spec.use_current         = true;
    spec.current_limit       = 100;
    spec.current_confirm_ms  = 20;
    spec.max_time_ms         = 5000;
    s_fixture.arrived_count  = 0;
    s_fixture.fault_count    = 0;
    result = motor_move_to(&s_executor, 0, motor_speed_gear(1), MOTOR_DIR_FORWARD, &spec);
    TEST_ASSERT_TRUE(motor_cmd_ok(result));
    s_fixture.current = 600;
    tick_at(0);
    tick_at(0);
    TEST_ASSERT_EQUAL_INT(MOTOR_PHASE_STOPPED, motor_phase(&s_executor, 0));
    TEST_ASSERT_EQUAL_INT(1, s_fixture.arrived_count);
    TEST_ASSERT_EQUAL_INT(MOTOR_END_CURRENT, s_fixture.last_arrived_trigger);
    TEST_ASSERT_EQUAL_INT(0, s_fixture.fault_count);

    /* 关闭电流停，仅过流监测 → 故障 */
    memset(&spec, 0, sizeof(spec));
    spec.use_time     = true;
    spec.duration_ms  = 5000;
    spec.max_time_ms  = 5000;
    s_fixture.arrived_count = 0;
    s_fixture.fault_count   = 0;
    result = motor_move_to(&s_executor, 0, motor_speed_gear(1), MOTOR_DIR_FORWARD, &spec);
    TEST_ASSERT_TRUE(motor_cmd_ok(result));
    s_fixture.current = 600;
    tick_at(0);
    tick_at(0);
    TEST_ASSERT_EQUAL_INT(MOTOR_PHASE_FAULT, motor_phase(&s_executor, 0));
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
    static motor_estop_t    estop;
    motor_config_t          config;
    motor_ports_t           ports;
    motor_init_result_t     result;

    memset(&s_fixture, 0, sizeof(s_fixture));
    memset(&s_executor, 0, sizeof(s_executor));
    memset(&config, 0, sizeof(config));
    s_fixture.zero_succeeds = true;

    driver = (motor_driver_t){
        .set_output = driver_set_output,
        .cutoff     = driver_cutoff,
        .reset      = driver_reset,
        .is_running = driver_is_running,
        .current    = driver_current,
        .ctx        = &s_fixture,
    };
    clock        = (motor_clock_t){clock_now, &s_fixture};
    sensors      = (motor_sensors_t){sensor_limit, &s_fixture};
    estop        = (motor_estop_t){estop_active, &s_fixture};
    drivers[0]   = &driver;
    encoders[0]  = NULL;
    ports        = (motor_ports_t){
               .clock    = &clock,
               .drivers  = drivers,
               .encoders = encoders,
               .sensors  = &sensors,
               .estop    = &estop,
    };
    config.motor_count                   = 1;
    config.driver_count                  = 1;
    config.tick_ms                       = 10;
    config.watchdog_ms                   = 100;
    config.motors[0].driver_index        = 0;
    config.motors[0].has_encoder         = false;
    config.motors[0].cap_position_move   = false;
    config.motors[0].default_max_move_ms = 1000;
    config.motors[0].gear_count          = 2;

    result = motor_init(&s_executor, &config, &ports);
    TEST_ASSERT_TRUE_MESSAGE(result.ok, result.error);
    TEST_ASSERT_TRUE(motor_baseline_trusted(&s_executor, 0));
    TEST_ASSERT_TRUE(motor_encoder_healthy(&s_executor, 0));
}

/** @brief POS|NEG 掩码：任一硬限位触发即停，并回报实际触发种类。 */
static void test_limit_mask_pos_or_neg_stops(void)
{
    motor_move_spec_t  spec = {0};
    motor_cmd_result_t result;

    init_executor(0, MOTOR_ENC_ABSOLUTE);
    spec.limit_mask  = (uint8_t)(MOTOR_LIMIT_MASK_POS | MOTOR_LIMIT_MASK_NEG);
    spec.max_time_ms = 5000;
    result           = motor_move_to(&s_executor, 0, motor_speed_gear(1), MOTOR_DIR_FORWARD, &spec);
    TEST_ASSERT_TRUE(motor_cmd_ok(result));

    tick_at(0);
    TEST_ASSERT_EQUAL_INT(MOTOR_PHASE_RUNNING, motor_phase(&s_executor, 0));

    s_fixture.neg_limit_active = true;
    tick_at(0);
    TEST_ASSERT_EQUAL_INT(MOTOR_PHASE_STOPPED, motor_phase(&s_executor, 0));
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
    result           = motor_move_to(&s_executor, 0, motor_speed_gear(1), MOTOR_DIR_FORWARD, &spec);
    TEST_ASSERT_TRUE(motor_cmd_ok(result));

    s_fixture.origin_active    = true;
    s_fixture.pos_limit_active = true;
    tick_at(0);
    TEST_ASSERT_EQUAL_INT(MOTOR_PHASE_STOPPED, motor_phase(&s_executor, 0));
    TEST_ASSERT_EQUAL_INT(MOTOR_END_LIMIT, s_fixture.last_arrived_trigger);
    TEST_ASSERT_EQUAL_INT(MOTOR_LIMIT_ORIGIN, s_fixture.last_arrived_limit);
}


static void test_motor_capacity_constants(void)
{
    TEST_ASSERT_EQUAL_INT(16, MOTOR_MAX_MOTORS);
    TEST_ASSERT_EQUAL_INT(16, MOTOR_MAX_DRIVERS);
    TEST_ASSERT_EQUAL_INT(16, MOTOR_MAX_INTERLOCKS);
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
    WDF_RUN_TEST(test_origin_external_stop_while_pressed_rebuilds_baseline,
                 "",
                 "验证压原点时外部停止仍重建基准");
    WDF_RUN_TEST(test_no_encoder_baseline_trusted_at_init, "", "验证无编码器轴上电基准恒可信");
    WDF_RUN_TEST(
        test_origin_clear_failure_keeps_baseline_untrusted_and_faults, "", "验证原点清除失败保持基线不可信并进入故障");
    WDF_RUN_TEST(
        test_unhealthy_encoder_rejects_position_move_but_allows_homing, "", "验证不健康编码器拒绝位置移动但允许回零");
    WDF_RUN_TEST(test_homing_restores_encoder_health, "", "验证回零恢复编码器健康状态");
    WDF_RUN_TEST(test_current_stop_arrives_after_confirm, "", "验证电流停经确认后正常到位");
    WDF_RUN_TEST(test_current_stop_respects_blank_ms, "", "验证电流停尊重启动消隐");
    WDF_RUN_TEST(test_current_stop_coexists_with_overcurrent_fault, "", "验证电流停与过流故障共存");
    WDF_RUN_TEST(test_limit_mask_pos_or_neg_stops, "", "验证硬限位掩码正负任一触发即停");
    WDF_RUN_TEST(test_limit_mask_prefers_origin_when_multiple, "", "验证硬限位同拍多路优先原点");
    return UNITY_END();
}
