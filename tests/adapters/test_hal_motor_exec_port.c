/**
 * @file    test_hal_motor_exec_port.c
 * @brief   电机执行器出站端口（hal_motor_*）单元测试。
 */

#include "adapters/outbound/hal/components/motor_exec/hal_motor_executor.h"
#include "domain/ports/outbound/motor/hal_motor_exec_port.h"
#include "wdf_test_spec.h"

#include <stdbool.h>
#include <string.h>

typedef struct {
    uint64_t now_ms;
    int64_t  position;
    int      output_count;
    int      cutoff_count;
} port_fixture_t;

static port_fixture_t   s_fx;
static motor_executor_t s_exec;
static motor_driver_t   s_driver;
static motor_encoder_t  s_encoder;
static motor_sensors_t  s_sensors;
static motor_estop_t    s_estop;
static motor_clock_t    s_clock;
static motor_driver_t  *s_drivers[1];
static motor_encoder_t *s_encoders[1];

static uint64_t clock_now(void *ctx)
{
    return ((port_fixture_t *)ctx)->now_ms;
}

static sw_err_t driver_set_output(void *ctx, motor_speed_t speed, motor_direction_t dir)
{
    port_fixture_t *fx = (port_fixture_t *)ctx;
    (void)speed;
    (void)dir;
    fx->output_count++;
    return SW_OK;
}

static sw_err_t driver_cutoff(void *ctx)
{
    ((port_fixture_t *)ctx)->cutoff_count++;
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

static bool estop_active(void *ctx)
{
    (void)ctx;
    return false;
}

static void init_executor(void)
{
    motor_config_t cfg;
    motor_ports_t  ports;
    motor_init_result_t ir;

    memset(&s_fx, 0, sizeof(s_fx));
    memset(&s_exec, 0, sizeof(s_exec));
    memset(&cfg, 0, sizeof(cfg));

    s_clock.now_ms = clock_now;
    s_clock.ctx    = &s_fx;

    s_driver.set_output  = driver_set_output;
    s_driver.cutoff      = driver_cutoff;
    s_driver.reset       = driver_reset;
    s_driver.is_running  = driver_is_running;
    s_driver.current     = driver_current;
    s_driver.ctx         = &s_fx;
    s_drivers[0]         = &s_driver;

    s_encoder.raw = encoder_raw;
    s_encoder.zero = encoder_zero;
    s_encoder.ctx  = &s_fx;
    s_encoders[0]  = &s_encoder;

    s_sensors.limit = sensor_limit;
    s_sensors.ctx   = &s_fx;
    s_estop.active  = estop_active;
    s_estop.ctx     = &s_fx;

    ports.clock    = &s_clock;
    ports.drivers  = s_drivers;
    ports.encoders = s_encoders;
    ports.sensors  = &s_sensors;
    ports.estop    = &s_estop;

    cfg.motor_count            = 1;
    cfg.driver_count           = 1;
    cfg.tick_ms                = 10;
    cfg.watchdog_ms            = 1000;
    cfg.motors[0].driver_index = 0;
    cfg.motors[0].has_encoder  = true;
    cfg.motors[0].encoder_kind = MOTOR_ENC_INCREMENTAL;
    cfg.motors[0].cap_position_move = true;
    cfg.motors[0].default_max_move_ms = 5000;
    cfg.motors[0].gear_count   = 5;
    cfg.motors[0].pos_tolerance = 10;

    ir = motor_init(&s_exec, &cfg, &ports);
    TEST_ASSERT_TRUE(ir.ok);
}

void setUp(void)
{
    init_executor();
}

void tearDown(void)
{
}

static void test_run_continuous_and_phase_via_port(void)
{
    hal_motor_cmd_result_t r;
    hal_motor_exec_t      *hal = (hal_motor_exec_t *)&s_exec;

    r = hal_motor_run_continuous(hal, 0, hal_motor_speed_gear(2), HAL_MOTOR_DIR_REVERSE);
    TEST_ASSERT_TRUE(hal_motor_cmd_ok(r));
    motor_tick(&s_exec);
    TEST_ASSERT_EQUAL_INT(HAL_MOTOR_PHASE_RUNNING, hal_motor_phase(hal, 0));
    TEST_ASSERT_EQUAL_INT(HAL_MOTOR_DIR_REVERSE, hal_motor_direction(hal, 0));
    TEST_ASSERT_TRUE(s_fx.output_count > 0);
}

static void test_move_to_time_and_stop_via_port(void)
{
    hal_motor_move_spec_t  spec;
    hal_motor_cmd_result_t r;
    hal_motor_exec_t      *hal = (hal_motor_exec_t *)&s_exec;

    memset(&spec, 0, sizeof(spec));
    spec.use_time    = true;
    spec.duration_ms = 30;
    spec.max_time_ms = 1000;

    r = hal_motor_move_to(hal, 0, hal_motor_speed_freq(1000), HAL_MOTOR_DIR_FORWARD, &spec);
    TEST_ASSERT_TRUE(hal_motor_cmd_ok(r));
    motor_tick(&s_exec);
    TEST_ASSERT_EQUAL_INT(HAL_MOTOR_PHASE_RUNNING, hal_motor_phase(hal, 0));

    r = hal_motor_stop(hal, 0);
    TEST_ASSERT_TRUE(hal_motor_cmd_ok(r));
    motor_tick(&s_exec);
    TEST_ASSERT_EQUAL_INT(HAL_MOTOR_PHASE_STOPPED, hal_motor_phase(hal, 0));
}

static void test_set_speed_and_recover_via_port(void)
{
    hal_motor_cmd_result_t r;
    hal_motor_exec_t      *hal = (hal_motor_exec_t *)&s_exec;

    r = hal_motor_run_continuous(hal, 0, hal_motor_speed_gear(1), HAL_MOTOR_DIR_FORWARD);
    TEST_ASSERT_TRUE(hal_motor_cmd_ok(r));
    motor_tick(&s_exec);

    r = hal_motor_set_speed(hal, 0, hal_motor_speed_gear(3), HAL_MOTOR_DIR_FORWARD);
    TEST_ASSERT_TRUE(hal_motor_cmd_ok(r));

    r = hal_motor_stop(hal, 0);
    TEST_ASSERT_TRUE(hal_motor_cmd_ok(r));
    motor_tick(&s_exec);

    s_exec.m[0].phase      = MOTOR_PHASE_FAULT;
    s_exec.m[0].fault_code = MOTOR_FAULT_OVERCURRENT;
    TEST_ASSERT_EQUAL_INT(HAL_MOTOR_FAULT_OVERCURRENT, hal_motor_fault_code(hal, 0));

    r = hal_motor_recover(hal, 0, HAL_MOTOR_RECOVERY_DRIVER_RESET);
    TEST_ASSERT_TRUE(hal_motor_cmd_ok(r));
    r = hal_motor_recover(hal, 0, HAL_MOTOR_RECOVERY_MODULE_STOP);
    TEST_ASSERT_TRUE(hal_motor_cmd_ok(r));
    TEST_ASSERT_EQUAL_INT(HAL_MOTOR_PHASE_STOPPED, hal_motor_phase(hal, 0));
}

static void test_query_helpers_via_port(void)
{
    hal_motor_exec_t *hal = (hal_motor_exec_t *)&s_exec;

    s_fx.position = 42;
    s_exec.m[0].position         = 42;
    s_exec.m[0].baseline_trusted = true;
    s_exec.m[0].enc_healthy      = true;

    TEST_ASSERT_EQUAL_INT64(42, hal_motor_position(hal, 0));
    TEST_ASSERT_TRUE(hal_motor_baseline_trusted(hal, 0));
    TEST_ASSERT_TRUE(hal_motor_encoder_healthy(hal, 0));

    s_exec.m[0].baseline_trusted = false;
    s_exec.m[0].enc_healthy      = false;
    TEST_ASSERT_FALSE(hal_motor_baseline_trusted(hal, 0));
    TEST_ASSERT_FALSE(hal_motor_encoder_healthy(hal, 0));

    /* 无编码器时查询侧恒 true，与字段值无关。 */
    s_exec.cfg.motors[0].has_encoder = false;
    TEST_ASSERT_TRUE(hal_motor_baseline_trusted(hal, 0));
    TEST_ASSERT_TRUE(hal_motor_encoder_healthy(hal, 0));
    s_exec.cfg.motors[0].has_encoder = true;
}

static void test_pop_event_via_port(void)
{
    motor_event_t     ev;
    hal_motor_event_t out;
    hal_motor_exec_t *hal = (hal_motor_exec_t *)&s_exec;

    memset(&ev, 0, sizeof(ev));
    ev.motor      = 0;
    ev.type       = MOTOR_EVENT_ARRIVED;
    ev.trigger    = MOTOR_END_TIME;
    ev.final_pos  = 10;
    ev.elapsed_ms = 20;
    ev.fault      = MOTOR_FAULT_NONE;
    /* 直接压入执行器队列 */
    s_exec.events[0] = ev;
    s_exec.ev_head   = 0;
    s_exec.ev_count  = 1;

    TEST_ASSERT_TRUE(hal_motor_pop_event(hal, &out));
    TEST_ASSERT_EQUAL_INT(HAL_MOTOR_EVENT_ARRIVED, out.type);
    TEST_ASSERT_EQUAL_INT(HAL_MOTOR_END_TIME, out.trigger);
    TEST_ASSERT_EQUAL_INT64(10, out.final_pos);
    TEST_ASSERT_EQUAL_UINT64(20, out.elapsed_ms);
    TEST_ASSERT_FALSE(hal_motor_pop_event(hal, &out));
    TEST_ASSERT_FALSE(hal_motor_pop_event(hal, NULL));
}

static void test_pop_event_for_keeps_other_motors(void)
{
    motor_event_t     ev0;
    motor_event_t     ev1;
    hal_motor_event_t out;
    hal_motor_exec_t *hal = (hal_motor_exec_t *)&s_exec;

    memset(&ev0, 0, sizeof(ev0));
    ev0.motor   = 0;
    ev0.type    = MOTOR_EVENT_ARRIVED;
    ev0.trigger = MOTOR_END_LIMIT;
    ev0.has_limit = true;
    ev0.limit   = MOTOR_LIMIT_ORIGIN;

    memset(&ev1, 0, sizeof(ev1));
    ev1.motor   = 1;
    ev1.type    = MOTOR_EVENT_TIMEOUT;
    ev1.trigger = MOTOR_END_TIMEOUT;

    s_exec.events[0] = ev1;
    s_exec.events[1] = ev0;
    s_exec.ev_head   = 0;
    s_exec.ev_count  = 2;

    TEST_ASSERT_TRUE(hal_motor_pop_event_for(hal, 0, &out));
    TEST_ASSERT_EQUAL_INT(0, out.motor);
    TEST_ASSERT_EQUAL_INT(HAL_MOTOR_END_LIMIT, out.trigger);
    TEST_ASSERT_TRUE(out.has_limit);
    TEST_ASSERT_EQUAL_INT(HAL_MOTOR_LIMIT_ORIGIN, out.limit);

    TEST_ASSERT_FALSE(hal_motor_pop_event_for(hal, 0, &out));
    TEST_ASSERT_TRUE(hal_motor_pop_event(hal, &out));
    TEST_ASSERT_EQUAL_INT(1, out.motor);
    TEST_ASSERT_EQUAL_INT(HAL_MOTOR_END_TIMEOUT, out.trigger);
}

/**
 * @brief 队列满时优先丢同电机最旧，不挤掉其它电机未消费事件
 */
static void test_event_queue_prefers_drop_same_motor(void)
{
    motor_event_t     ev;
    hal_motor_event_t out;
    hal_motor_exec_t *hal = (hal_motor_exec_t *)&s_exec;
    int               i;
    int               motor1_left;

    /* 队头起 63 条电机 1，末条电机 0；再让电机 0 因按时到位压入新事件。 */
    for (i = 0; i < MOTOR_EVENT_QUEUE_CAP; ++i) {
        memset(&ev, 0, sizeof(ev));
        ev.motor      = (i == MOTOR_EVENT_QUEUE_CAP - 1) ? 0 : 1;
        ev.type       = MOTOR_EVENT_WARNING;
        ev.elapsed_ms = (uint64_t)i;
        s_exec.events[i] = ev;
    }
    s_exec.ev_head  = 0;
    s_exec.ev_count = MOTOR_EVENT_QUEUE_CAP;

    s_exec.m[0].phase        = MOTOR_PHASE_RUNNING;
    s_exec.m[0].move_active  = true;
    s_exec.m[0].move_start_ms = 0;
    s_exec.m[0].elapsed_ms   = 0;
    memset(&s_exec.m[0].spec, 0, sizeof(s_exec.m[0].spec));
    s_exec.m[0].spec.use_time    = true;
    s_exec.m[0].spec.duration_ms = 0;
    s_exec.m[0].spec.max_time_ms = 1000;
    s_fx.now_ms = 10;
    motor_tick(&s_exec);

    motor1_left = 0;
    while (hal_motor_pop_event(hal, &out)) {
        if (out.motor == 1) {
            motor1_left++;
        }
    }
    /* 旧策略会先丢队头电机 1；新策略只丢末尾电机 0，电机 1 仍满 63 条。 */
    TEST_ASSERT_EQUAL_INT(MOTOR_EVENT_QUEUE_CAP - 1, motor1_left);
}

int main(void)
{
    UNITY_BEGIN();

    WDF_RUN_TEST(test_run_continuous_and_phase_via_port, "", "验证经端口连续运行与相位查询");
    WDF_RUN_TEST(test_move_to_time_and_stop_via_port, "", "验证经端口按时到位与停止");
    WDF_RUN_TEST(test_set_speed_and_recover_via_port, "", "验证经端口调速与故障恢复");
    WDF_RUN_TEST(test_query_helpers_via_port, "", "验证经端口位置与基准/编码器查询");
    WDF_RUN_TEST(test_pop_event_via_port, "", "验证经端口取出运动事件");
    WDF_RUN_TEST(test_pop_event_for_keeps_other_motors, "", "验证按电机取事件保留其它电机");
    WDF_RUN_TEST(test_event_queue_prefers_drop_same_motor, "", "验证队列满时优先丢同电机事件");

    return UNITY_END();
}
