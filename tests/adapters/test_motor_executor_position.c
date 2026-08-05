/**
 * @file test_motor_executor_position.c
 * @brief MCC 挡位位置控制单元测试
 */

#include "adapters/outbound/hal/providers/mcc/motor_executor.h"
#include "wdf_test_spec.h"

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

typedef struct {
    uint64_t          now_ms;
    int64_t           position;
    int               output_count;
    int               cutoff_count;
    int               zero_count;
    bool              zero_succeeds;
    bool              origin_active;
    int               arrived_count;
    int               timeout_count;
    int               fault_count;
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
    (void)ctx;
    return 0;
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
    (void)motor;
    return (kind == MOTOR_LIMIT_ORIGIN) && ((position_fixture_t *)ctx)->origin_active;
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
    } else if (event->type == MOTOR_EVENT_TIMEOUT) {
        fixture->timeout_count++;
    } else if (event->type == MOTOR_EVENT_FAULT) {
        fixture->fault_count++;
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
    spec.use_limit = true;
    spec.limit     = MOTOR_LIMIT_ORIGIN;
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

    init_executor(40, MOTOR_ENC_INCREMENTAL);
    /* 连续 3 拍无脉冲即判定停滞；夹具默认不配置该阈值，检测不启用。 */
    s_executor.cfg.motors[0].enc_stall_ticks = 3;

    /* 先归位建立可信基准，否则位置运动会先被 baseline-untrusted 拦下，
     * 无法区分是基准未建立还是编码器不健康。限位须在运动开始后才生效。 */
    result = motor_home(&s_executor, 0);
    TEST_ASSERT_TRUE(motor_cmd_ok(result));
    tick_at(45);
    s_fixture.origin_active = true;
    tick_at(50);
    TEST_ASSERT_TRUE(motor_baseline_trusted(&s_executor, 0));
    TEST_ASSERT_TRUE(motor_encoder_healthy(&s_executor, 0));

    /* 运动中冻结脉冲：位置读数不再变化，累计到阈值后判定编码器不健康。 */
    s_fixture.origin_active = false;
    spec.use_position       = true;
    spec.target_pos         = 500;
    result                  = motor_move_to(&s_executor, 0, motor_speed_gear(1), MOTOR_DIR_FORWARD, &spec);
    TEST_ASSERT_TRUE(motor_cmd_ok(result));
    for (i = 0; i < 5; ++i) {
        tick_at(50);
    }
    TEST_ASSERT_FALSE(motor_encoder_healthy(&s_executor, 0));

    /* 位置运动被拒，理由是编码器不健康而非基准未建立。 */
    result = motor_move_to(&s_executor, 0, motor_speed_gear(1), MOTOR_DIR_FORWARD, &spec);
    TEST_ASSERT_FALSE(motor_cmd_ok(result));

    /* 归位仍可下发，机器据此自行恢复。归位要求电机已停，先停当前运动。 */
    TEST_ASSERT_TRUE(motor_cmd_ok(motor_stop(&s_executor, 0)));
    tick_at(50);
    result = motor_home(&s_executor, 0);
    TEST_ASSERT_TRUE(motor_cmd_ok(result));
}

/**
 * @brief  归位重建基准后编码器恢复健康
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

    /* 冻结脉冲使编码器判定为不健康。 */
    s_fixture.origin_active = false;
    TEST_ASSERT_TRUE(motor_cmd_ok(motor_move_to(&s_executor, 0, motor_speed_gear(1), MOTOR_DIR_FORWARD, &spec)));
    for (i = 0; i < 5; ++i) {
        tick_at(50);
    }
    TEST_ASSERT_FALSE(motor_encoder_healthy(&s_executor, 0));

    /* 归位撞上原点限位：清零基准的同一路径恢复健康。归位要求电机已停。 */
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
    spec.use_limit          = true;
    spec.limit              = MOTOR_LIMIT_ORIGIN;
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

int main(void)
{
    UNITY_BEGIN();
    WDF_RUN_TEST(test_forward_position_move_slows_once_and_stops_after_overshoot,
                 "",
                 "验证正向位置移动仅减速一次并在越过目标后停止");
    WDF_RUN_TEST(test_reverse_position_move_stops_after_overshoot, "", "验证反向位置移动在越过目标后停止");
    WDF_RUN_TEST(test_active_position_target_update_keeps_start_time_and_output,
                 "",
                 "验证活动状态位置目标更新保持启动时间并输出");
    WDF_RUN_TEST(test_origin_move_clears_hardware_and_software_once, "", "验证原点移动仅清除一次软硬件位置");
    WDF_RUN_TEST(
        test_origin_clear_failure_keeps_baseline_untrusted_and_faults, "", "验证原点清除失败保持基线不可信并进入故障");
    WDF_RUN_TEST(
        test_unhealthy_encoder_rejects_position_move_but_allows_homing, "", "验证不健康编码器拒绝位置移动但允许回零");
    WDF_RUN_TEST(test_homing_restores_encoder_health, "", "验证回零恢复编码器健康状态");
    return UNITY_END();
}
