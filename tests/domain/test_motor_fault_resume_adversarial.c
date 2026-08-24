/**
 * @file    test_motor_fault_resume_adversarial.c
 * @brief   motor-fault-resume-policy 对抗式测试
 *
 * @note    以破坏者角色覆盖状态机边界、时序竞态、资源枯竭、重入、故障组合、
 *          边界值与初始化依赖。不修改被测源码。每个用例标注攻击目标与手法，
 *          且彼此独立。
 */

#include "common/sw_error.h"
#include "domain/mechanism/motor/motor_executor.h"
#include "domain/ports/outbound/motor/motor_exec_port.h"
#include "domain/ports/outbound/safety/safety_output_hold.h"
#include "wdf_test_spec.h"

#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

enum {
    ADV_CONC_ITERS = 200, /**< 并发线程每侧迭代次数 */
    ADV_HAMMER_N   = 8    /**< 连打 run / 填事件槽的次数 */
};

typedef struct {
    uint64_t            now_ms;         /**< 模拟时钟 */
    int64_t             position;       /**< 编码器 raw */
    int                 output_count;   /**< set_output 次数 */
    int                 cutoff_count;   /**< cutoff 次数 */
    int                 current;        /**< 负载电流 */
    int                 reset_count;    /**< 驱动复位次数 */
    int                 temperature;    /**< 温度读数 */
    bool                temperature_ok; /**< 温度端口是否可读 */
    bool                running;        /**< 功率级是否在转 */
    bool                reset_ok;       /**< reset 返回值 */
    sw_err_t            set_output_rc;  /**< set_output 返回码 */
    motor_port_status_t port_status;    /**< 端口健康 */
    motor_speed_t       last_speed;     /**< 最近一次速度给定 */
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

static int                s_reset_reenter_depth; /**< reset 回调重入深度 */
static motor_cmd_result_t s_nested_run_result;   /**< 回调内嵌套 run 的结果 */
static volatile int       s_conc_run_ok;         /**< 需确认 FAULT 下 run 被受理次数 */
static volatile int       s_conc_running;        /**< 需确认 FAULT 下进入 RUNNING 次数 */
static volatile int       s_conc_query_mismatch; /**< 查询与配置不一致次数 */

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
    return fx->set_output_rc;
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

/**
 * @brief  复位回调中再次调用 run，攻击递归锁下的半清窗口
 * @param  ctx  端口夹具
 * @return 外层 reset_ok
 */
static bool driver_reset_reenter_run(void *ctx)
{
    port_fixture_t *fx = (port_fixture_t *)ctx;

    fx->reset_count++;
    if (s_reset_reenter_depth == 0) {
        s_reset_reenter_depth = 1;
        s_nested_run_result   = motor_exec_run(s_exec, 0, motor_speed_gear(1), MOTOR_DIR_FORWARD, NULL);
        s_reset_reenter_depth = 0;
    }
    return fx->reset_ok;
}

static bool driver_is_running(void *ctx)
{
    return ((port_fixture_t *)ctx)->running;
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

/**
 * @brief  绑定默认单轴执行器（confirm_faults=0，过流可续动）
 */
static void init_executor(void)
{
    motor_init_result_t ir;

    memset(&s_fx, 0, sizeof(s_fx));
    s_fx.running       = true;
    s_fx.reset_ok      = true;
    s_fx.set_output_rc = SW_OK;
    s_fx.port_status   = MOTOR_PORT_OK;
    memset(&s_cfg, 0, sizeof(s_cfg));
    memset(&s_driver, 0, sizeof(s_driver));
    memset(&s_encoder, 0, sizeof(s_encoder));
    memset(s_drivers, 0, sizeof(s_drivers));
    memset(s_encoders, 0, sizeof(s_encoders));
    motor_executor_test_reset();
    safety_output_hold_reset();
    s_exec                     = NULL;
    s_reset_reenter_depth      = 0;
    s_nested_run_result.status = MOTOR_CMD_REJECTED;
    s_nested_run_result.reject = MOTOR_REJECT_NONE;
    s_nested_run_result.reason = "";

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

/**
 * @brief  启用过流监测（确认时间 20ms = 两拍）
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
 * @brief  启用过温监测（确认时间 20ms = 两拍）
 */
static void enable_overtemp_monitor(void)
{
    s_cfg.motors[0].mon.monitor_temp    = true;
    s_cfg.motors[0].mon.temp_max        = 80;
    s_cfg.motors[0].mon.temp_confirm_ms = 20;
    s_driver.temperature                = driver_temperature;
    s_fx.temperature_ok                 = true;
    s_fx.temperature                    = 0;
}

/**
 * @brief  运行至过流 FAULT
 * @param  hal  已绑定的执行器
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

/**
 * @brief  运行至过温 FAULT（先以正常电流启动）
 * @param  hal  已绑定的执行器
 */
static void run_until_overtemp(motor_exec_t *hal)
{
    motor_cmd_result_t r;

    s_fx.current     = 0;
    s_fx.temperature = 0;
    r                = motor_exec_run(hal, 0, motor_speed_gear(1), MOTOR_DIR_FORWARD, NULL);
    TEST_ASSERT_TRUE(motor_cmd_ok(r));
    motor_executor_tick(s_exec);
    TEST_ASSERT_EQUAL_INT(MOTOR_STATE_RUNNING, motor_exec_state(hal, 0));

    s_fx.temperature = 100;
    s_fx.now_ms      = 10;
    motor_executor_tick(s_exec);
    s_fx.now_ms = 20;
    motor_executor_tick(s_exec);
    TEST_ASSERT_EQUAL_INT(MOTOR_STATE_FAULT, motor_exec_state(hal, 0));
    TEST_ASSERT_EQUAL_INT(MOTOR_FAULT_OVERTEMP, motor_exec_fault_code(hal, 0));
}

void setUp(void)
{
    init_executor();
}

void tearDown(void)
{
}

/* TC-01: 需确认 FAULT 下同一事件重复触发 */
/* 攻击目标: INV-01；非法转换「需确认 FAULT 下受理运动」 */
void test_confirm_fault_repeat_run_home_stay_fault(void)
{
    motor_cmd_result_t r;
    motor_exec_t      *hal;
    int                resets;
    int                i;

    enable_overtemp_monitor();
    s_cfg.motors[0].confirm_faults = motor_fault_confirm_bit(MOTOR_FAULT_OVERTEMP);
    rebind_executor();
    hal = s_exec;
    run_until_overtemp(hal);
    resets = s_fx.reset_count;

    for (i = 0; i < ADV_HAMMER_N; ++i) {
        r = motor_exec_run(hal, 0, motor_speed_gear(1), MOTOR_DIR_FORWARD, NULL);
        TEST_ASSERT_FALSE(motor_cmd_ok(r));
        TEST_ASSERT_EQUAL_INT(MOTOR_REJECT_FAULT, r.reject);
        r = motor_exec_home(hal, 0);
        TEST_ASSERT_FALSE(motor_cmd_ok(r));
        TEST_ASSERT_EQUAL_INT(MOTOR_REJECT_FAULT, r.reject);
    }

    TEST_ASSERT_EQUAL_INT(MOTOR_STATE_FAULT, motor_exec_state(hal, 0));
    TEST_ASSERT_EQUAL_INT(MOTOR_FAULT_OVERTEMP, motor_exec_fault_code(hal, 0));
    TEST_ASSERT_EQUAL_INT(resets, s_fx.reset_count);
}

/* TC-02: 可续动 FAULT 上 home 必须内清再启动 */
/* 攻击目标: 「可续动在 run/home 内清除」；试图让 home 仍按旧拒令路径拒绝 */
void test_resumable_fault_home_clears_and_starts(void)
{
    motor_cmd_result_t r;
    motor_exec_t      *hal;
    int                resets;

    enable_overcurrent_monitor();
    rebind_executor();
    hal = s_exec;
    run_until_overcurrent(hal);
    resets       = s_fx.reset_count;
    s_fx.current = 0;

    r = motor_exec_home(hal, 0);
    TEST_ASSERT_TRUE(motor_cmd_ok(r));
    TEST_ASSERT_NOT_EQUAL(MOTOR_STATE_FAULT, motor_exec_state(hal, 0));
    TEST_ASSERT_EQUAL_INT(MOTOR_FAULT_NONE, motor_exec_fault_code(hal, 0));
    TEST_ASSERT_TRUE(s_fx.reset_count > resets);
}

/* TC-03: 恢复步骤颠倒、只做半步后 run */
/* 攻击目标: 需确认码必须两步 recover；试图用 MODULE_STOP 抢跑或半步后运动 */
void test_confirm_partial_recover_then_run_rejected(void)
{
    motor_cmd_result_t r;
    motor_exec_t      *hal;

    enable_overtemp_monitor();
    s_cfg.motors[0].confirm_faults = motor_fault_confirm_bit(MOTOR_FAULT_OVERTEMP);
    rebind_executor();
    hal = s_exec;
    run_until_overtemp(hal);

    r = motor_exec_recover(hal, 0, MOTOR_RECOVERY_MODULE_STOP);
    TEST_ASSERT_FALSE(motor_cmd_ok(r));
    TEST_ASSERT_EQUAL_INT(MOTOR_REJECT_MUST_RESET, r.reject);
    TEST_ASSERT_EQUAL_INT(MOTOR_STATE_FAULT, motor_exec_state(hal, 0));

    r = motor_exec_recover(hal, 0, MOTOR_RECOVERY_DRIVER_RESET);
    TEST_ASSERT_TRUE(motor_cmd_ok(r));
    TEST_ASSERT_EQUAL_INT(MOTOR_STATE_FAULT, motor_exec_state(hal, 0));

    r = motor_exec_run(hal, 0, motor_speed_gear(1), MOTOR_DIR_FORWARD, NULL);
    TEST_ASSERT_FALSE(motor_cmd_ok(r));
    TEST_ASSERT_EQUAL_INT(MOTOR_REJECT_FAULT, r.reject);
    TEST_ASSERT_EQUAL_INT(MOTOR_FAULT_OVERTEMP, motor_exec_fault_code(hal, 0));
}

/**
 * @brief  并发 run 线程：需确认 FAULT 下受理即记一次违规
 */
static void *conc_run_thread(void *arg)
{
    int i;

    (void)arg;
    for (i = 0; i < ADV_CONC_ITERS; ++i) {
        motor_cmd_result_t r = motor_exec_run(s_exec, 0, motor_speed_gear(1), MOTOR_DIR_FORWARD, NULL);

        if (motor_cmd_ok(r)) {
            s_conc_run_ok++;
        }
        if (motor_exec_state(s_exec, 0) == MOTOR_STATE_RUNNING) {
            s_conc_running++;
        }
    }
    return NULL;
}

/**
 * @brief  并发 tick 线程：在未保护窗口推进监测
 */
static void *conc_tick_thread(void *arg)
{
    int i;

    (void)arg;
    for (i = 0; i < ADV_CONC_ITERS; ++i) {
        s_fx.now_ms += 1U;
        motor_executor_tick(s_exec);
    }
    return NULL;
}

/**
 * @brief  并发查询线程：位图在运行期必须保持需确认
 */
static void *conc_query_thread(void *arg)
{
    int i;

    (void)arg;
    for (i = 0; i < ADV_CONC_ITERS; ++i) {
        if (!motor_exec_fault_requires_confirm(s_exec, 0, MOTOR_FAULT_OVERTEMP)) {
            s_conc_query_mismatch++;
        }
    }
    return NULL;
}

/* TC-04: 需确认 FAULT 下 tick/run/查询并发 */
/* 攻击目标: INV-05 命令与 tick 由执行器锁串行；试图在未保护窗口放行 */
void test_confirm_fault_concurrent_tick_run_query(void)
{
    pthread_t t_run;
    pthread_t t_tick;
    pthread_t t_query;

    enable_overtemp_monitor();
    s_cfg.motors[0].confirm_faults = motor_fault_confirm_bit(MOTOR_FAULT_OVERTEMP);
    rebind_executor();
    run_until_overtemp(s_exec);

    s_conc_run_ok         = 0;
    s_conc_running        = 0;
    s_conc_query_mismatch = 0;

    TEST_ASSERT_EQUAL_INT(0, pthread_create(&t_run, NULL, conc_run_thread, NULL));
    TEST_ASSERT_EQUAL_INT(0, pthread_create(&t_tick, NULL, conc_tick_thread, NULL));
    TEST_ASSERT_EQUAL_INT(0, pthread_create(&t_query, NULL, conc_query_thread, NULL));
    TEST_ASSERT_EQUAL_INT(0, pthread_join(t_run, NULL));
    TEST_ASSERT_EQUAL_INT(0, pthread_join(t_tick, NULL));
    TEST_ASSERT_EQUAL_INT(0, pthread_join(t_query, NULL));

    TEST_ASSERT_EQUAL_INT(0, s_conc_run_ok);
    TEST_ASSERT_EQUAL_INT(0, s_conc_running);
    TEST_ASSERT_EQUAL_INT(0, s_conc_query_mismatch);
    TEST_ASSERT_EQUAL_INT(MOTOR_STATE_FAULT, motor_exec_state(s_exec, 0));
    TEST_ASSERT_EQUAL_INT(MOTOR_FAULT_OVERTEMP, motor_exec_fault_code(s_exec, 0));
}

/* TC-05: 连续内清失败打满事件槽 */
/* 攻击目标: 事件槽 MOTOR_EVENT_SLOT_CAP 溢出时仍保持该次 FAULT，不得自复 */
void test_resumable_reset_fail_fills_event_slot(void)
{
    motor_cmd_result_t r;
    motor_exec_t      *hal;
    motor_event_t      ev;
    int                popped;
    int                i;

    enable_overcurrent_monitor();
    rebind_executor();
    hal = s_exec;
    run_until_overcurrent(hal);

    while (motor_exec_pop_event_for(hal, 0, &ev)) {}

    s_fx.reset_ok = false;
    for (i = 0; i < (MOTOR_EVENT_SLOT_CAP + ADV_HAMMER_N); ++i) {
        r = motor_exec_run(hal, 0, motor_speed_gear(1), MOTOR_DIR_FORWARD, NULL);
        TEST_ASSERT_FALSE(motor_cmd_ok(r));
        TEST_ASSERT_EQUAL_INT(MOTOR_REJECT_DRIVER, r.reject);
    }

    TEST_ASSERT_EQUAL_INT(MOTOR_STATE_FAULT, motor_exec_state(hal, 0));
    TEST_ASSERT_EQUAL_INT(MOTOR_FAULT_OVERCURRENT, motor_exec_fault_code(hal, 0));

    popped = 0;
    while (motor_exec_pop_event_for(hal, 0, &ev)) {
        TEST_ASSERT_EQUAL_INT(MOTOR_EVENT_FAULT, ev.type);
        TEST_ASSERT_EQUAL_INT(MOTOR_FAULT_OVERCURRENT, ev.fault);
        popped++;
    }
    TEST_ASSERT_TRUE(popped <= MOTOR_EVENT_SLOT_CAP);
    TEST_ASSERT_TRUE(popped > 0);
}

/* TC-06: 可续动过流内清后电流仍超限，连打 run */
/* 攻击目标: 资源/重试耗尽；连打不得在仍为 FAULT 时给出功率级运动输出 */
void test_resumable_hammer_run_while_current_stays_high(void)
{
    motor_cmd_result_t r;
    motor_exec_t      *hal;
    int                i;

    enable_overcurrent_monitor();
    rebind_executor();
    hal = s_exec;
    run_until_overcurrent(hal);

    for (i = 0; i < ADV_HAMMER_N; ++i) {
        r = motor_exec_run(hal, 0, motor_speed_gear(1), MOTOR_DIR_FORWARD, NULL);
        TEST_ASSERT_TRUE(motor_cmd_ok(r));
        TEST_ASSERT_EQUAL_INT(MOTOR_FAULT_NONE, motor_exec_fault_code(hal, 0));
        TEST_ASSERT_NOT_EQUAL(MOTOR_STATE_FAULT, motor_exec_state(hal, 0));

        s_fx.now_ms += 10U;
        motor_executor_tick(s_exec);
        s_fx.now_ms += 10U;
        motor_executor_tick(s_exec);
        TEST_ASSERT_EQUAL_INT(MOTOR_STATE_FAULT, motor_exec_state(hal, 0));
        TEST_ASSERT_EQUAL_INT(MOTOR_FAULT_OVERCURRENT, motor_exec_fault_code(hal, 0));
    }
}

/* TC-07: 驱动复位回调中嵌套 run */
/* 攻击目标: 重入；半清窗口被二次 run 打断后不得 FAULT+RUNNING 并存 */
void test_reset_callback_nested_run_keeps_consistent_state(void)
{
    motor_cmd_result_t      r;
    motor_exec_t           *hal;
    motor_exec_state_t      st;
    motor_exec_fault_code_t code;

    enable_overcurrent_monitor();
    rebind_executor();
    hal = s_exec;
    run_until_overcurrent(hal);

    s_fx.current               = 0;
    s_driver.reset             = driver_reset_reenter_run;
    s_nested_run_result.status = MOTOR_CMD_REJECTED;

    r    = motor_exec_run(hal, 0, motor_speed_gear(1), MOTOR_DIR_FORWARD, NULL);
    st   = motor_exec_state(hal, 0);
    code = motor_exec_fault_code(hal, 0);

    TEST_ASSERT_FALSE((st == MOTOR_STATE_RUNNING) && (code != MOTOR_FAULT_NONE));
    TEST_ASSERT_FALSE((st == MOTOR_STATE_FAULT) && (code == MOTOR_FAULT_NONE));
    if (st == MOTOR_STATE_FAULT) {
        TEST_ASSERT_EQUAL_INT(MOTOR_FAULT_OVERCURRENT, code);
        TEST_ASSERT_FALSE(motor_cmd_ok(r) && motor_cmd_ok(s_nested_run_result));
    } else {
        TEST_ASSERT_EQUAL_INT(MOTOR_FAULT_NONE, code);
        TEST_ASSERT_TRUE(motor_cmd_ok(r) || motor_cmd_ok(s_nested_run_result));
    }
}

/* TC-08: 共享驱动连带 + 分轴策略 */
/* 攻击目标: 故障组合；轴 1 需确认 SHARED_DRIVER 不得因轴 0 可续动而被放行 */
void test_shared_driver_confirm_isolated_from_peer_resume(void)
{
    motor_cmd_result_t r;
    motor_exec_t      *hal;
    int                resets;

    enable_overcurrent_monitor();
    s_cfg.motor_count                   = 2;
    s_cfg.driver_count                  = 1;
    s_cfg.motors[1]                     = s_cfg.motors[0];
    s_cfg.motors[1].mon.monitor_current = false;
    s_cfg.motors[1].confirm_faults      = motor_fault_confirm_bit(MOTOR_FAULT_SHARED_DRIVER);
    s_cfg.motors[0].confirm_faults      = 0u;
    s_encoders[1]                       = &s_encoder;
    rebind_executor();
    hal = s_exec;

    TEST_ASSERT_FALSE(motor_exec_fault_requires_confirm(hal, 0, MOTOR_FAULT_OVERCURRENT));
    TEST_ASSERT_TRUE(motor_exec_fault_requires_confirm(hal, 1, MOTOR_FAULT_SHARED_DRIVER));

    run_until_overcurrent(hal);
    TEST_ASSERT_EQUAL_INT(MOTOR_STATE_FAULT, motor_exec_state(hal, 1));
    TEST_ASSERT_EQUAL_INT(MOTOR_FAULT_SHARED_DRIVER, motor_exec_fault_code(hal, 1));

    resets       = s_fx.reset_count;
    s_fx.current = 0;
    r            = motor_exec_run(hal, 1, motor_speed_gear(1), MOTOR_DIR_FORWARD, NULL);
    TEST_ASSERT_FALSE(motor_cmd_ok(r));
    TEST_ASSERT_EQUAL_INT(MOTOR_REJECT_FAULT, r.reject);
    TEST_ASSERT_EQUAL_INT(resets, s_fx.reset_count);
    TEST_ASSERT_EQUAL_INT(MOTOR_FAULT_SHARED_DRIVER, motor_exec_fault_code(hal, 1));

    r = motor_exec_run(hal, 0, motor_speed_gear(1), MOTOR_DIR_FORWARD, NULL);
    TEST_ASSERT_TRUE(motor_cmd_ok(r));
    TEST_ASSERT_EQUAL_INT(MOTOR_FAULT_NONE, motor_exec_fault_code(hal, 0));
    TEST_ASSERT_EQUAL_INT(MOTOR_STATE_FAULT, motor_exec_state(hal, 1));
}

/* TC-09: 可续动 FAULT 叠加 hold，再试图当可续动内清 */
/* 攻击目标: INV-03；hold 有效时不得按位图 0 放行 */
void test_hold_overrides_resumable_fault(void)
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
    r = motor_exec_run(hal, 0, motor_speed_gear(1), MOTOR_DIR_FORWARD, NULL);
    TEST_ASSERT_FALSE(motor_cmd_ok(r));
    TEST_ASSERT_EQUAL_INT(MOTOR_REJECT_SAFETY, r.reject);
    TEST_ASSERT_EQUAL_INT(resets, s_fx.reset_count);

    motor_executor_tick(s_exec);
    TEST_ASSERT_EQUAL_INT(MOTOR_STATE_ESTOP, motor_exec_state(hal, 0));
    r = motor_exec_run(hal, 0, motor_speed_gear(1), MOTOR_DIR_FORWARD, NULL);
    TEST_ASSERT_FALSE(motor_cmd_ok(r));
    TEST_ASSERT_EQUAL_INT(MOTOR_REJECT_SAFETY, r.reject);
    TEST_ASSERT_EQUAL_INT(resets, s_fx.reset_count);

    TEST_ASSERT_EQUAL_INT(SW_OK, safety_output_hold_release());
    motor_executor_tick(s_exec);
    /*
     * 攻击：trigger_estop 把 FAULT 改成 ESTOP 时不清理 fault_code，
     * 释放后 leave_estop 变成 STOPPED 却残留 OVERCURRENT。
     * 随后 run 只看 exec_state!=FAULT，会跳过内清直接启动（INV-02）。
     */
    TEST_ASSERT_TRUE_MESSAGE((motor_exec_state(hal, 0) == MOTOR_STATE_FAULT)
                                 || (motor_exec_fault_code(hal, 0) == MOTOR_FAULT_NONE),
                             "ESTOP 覆盖后不得留下 STOPPED+残留故障码");
}

/* TC-10: 看门狗安全态即使位图为 0 也不得内清 */
/* 攻击目标: INV-03；缺拍后 run 必须 SAFETY 拒绝且不复位驱动 */
void test_watchdog_safe_not_resumable(void)
{
    motor_cmd_result_t r;
    motor_exec_t      *hal;
    int                resets;

    enable_overcurrent_monitor();
    s_cfg.watchdog_ms = 50;
    rebind_executor();
    hal = s_exec;
    run_until_overcurrent(hal);
    resets = s_fx.reset_count;

    s_fx.now_ms = 80;
    motor_executor_tick(s_exec);
    TEST_ASSERT_TRUE(motor_executor_in_safe_state(hal));

    r = motor_exec_run(hal, 0, motor_speed_gear(1), MOTOR_DIR_FORWARD, NULL);
    TEST_ASSERT_FALSE(motor_cmd_ok(r));
    TEST_ASSERT_EQUAL_INT(MOTOR_REJECT_SAFETY, r.reject);
    TEST_ASSERT_EQUAL_INT(resets, s_fx.reset_count);
}

/* TC-11: 位图边界：NONE 位、越界位、合法满掩码、FATAL 硬覆盖 */
/* 攻击目标: 未定义位 bind 拒绝；FATAL 位即使列入也不得按可续动查询 */
void test_confirm_bitmap_edges_and_fatal_hard_override(void)
{
    motor_init_result_t     ir;
    motor_exec_t           *exec = NULL;
    motor_exec_fault_code_t code;

    s_cfg.motors[0].confirm_faults = 1u; /* NONE 位 */
    motor_executor_test_reset();
    safety_output_hold_reset();
    ir = motor_executor_bind(0U, &s_cfg, &s_ports, &exec);
    TEST_ASSERT_FALSE(ir.ok);
    TEST_ASSERT_NULL(exec);

    s_cfg.motors[0].confirm_faults = 1u << ((unsigned)MOTOR_FAULT_SHARED_DRIVER + 1u);
    motor_executor_test_reset();
    ir = motor_executor_bind(0U, &s_cfg, &s_ports, &exec);
    TEST_ASSERT_FALSE(ir.ok);

    s_cfg.motors[0].confirm_faults = UINT32_MAX;
    motor_executor_test_reset();
    ir = motor_executor_bind(0U, &s_cfg, &s_ports, &exec);
    TEST_ASSERT_FALSE(ir.ok);

    s_cfg.motors[0].confirm_faults = MOTOR_FAULT_CONFIRM_VALID_MASK;
    motor_executor_test_reset();
    ir = motor_executor_bind(0U, &s_cfg, &s_ports, &s_exec);
    TEST_ASSERT_TRUE_MESSAGE(ir.ok, ir.error);
    for (code = MOTOR_FAULT_OVERCURRENT; code <= MOTOR_FAULT_SHARED_DRIVER;
         code = (motor_exec_fault_code_t)((int)code + 1)) {
        TEST_ASSERT_TRUE(motor_exec_fault_requires_confirm(s_exec, 0, code));
    }
    TEST_ASSERT_TRUE(motor_exec_fault_requires_confirm(s_exec, 0, MOTOR_FAULT_DRIVER_PORT_FATAL));
    TEST_ASSERT_TRUE(motor_exec_fault_requires_confirm(s_exec, 0, MOTOR_FAULT_NONE));
    TEST_ASSERT_TRUE(motor_exec_fault_requires_confirm(s_exec, 0, (motor_exec_fault_code_t)99));

    s_cfg.motors[0].confirm_faults = motor_fault_confirm_bit(MOTOR_FAULT_DRIVER_PORT_FATAL);
    rebind_executor();
    TEST_ASSERT_TRUE(motor_exec_fault_requires_confirm(s_exec, 0, MOTOR_FAULT_DRIVER_PORT_FATAL));
    TEST_ASSERT_FALSE(motor_exec_fault_requires_confirm(s_exec, 0, MOTOR_FAULT_OVERCURRENT));
}

/* TC-12: 可续动 FAULT 上非法参数不得内清 */
/* 攻击目标: 边界值 0 / 最大值+1 / 非法方向 / 越界电机号；校验失败不得复位 */
void test_resumable_bad_params_do_not_inner_clear(void)
{
    motor_cmd_result_t r;
    motor_exec_t      *hal;
    int                resets;
    motor_move_spec_t  spec;

    enable_overcurrent_monitor();
    rebind_executor();
    hal = s_exec;
    run_until_overcurrent(hal);
    resets = s_fx.reset_count;

    r = motor_exec_run(hal, 0, motor_speed_gear(0), MOTOR_DIR_FORWARD, NULL);
    TEST_ASSERT_FALSE(motor_cmd_ok(r));
    TEST_ASSERT_EQUAL_INT(MOTOR_REJECT_BAD_SPEED, r.reject);

    r = motor_exec_run(hal, 0, motor_speed_gear(6), MOTOR_DIR_FORWARD, NULL);
    TEST_ASSERT_FALSE(motor_cmd_ok(r));
    TEST_ASSERT_EQUAL_INT(MOTOR_REJECT_BAD_SPEED, r.reject);

    r = motor_exec_run(hal, 0, motor_speed_freq(0), MOTOR_DIR_FORWARD, NULL);
    TEST_ASSERT_FALSE(motor_cmd_ok(r));
    TEST_ASSERT_EQUAL_INT(MOTOR_REJECT_BAD_SPEED, r.reject);

    r = motor_exec_run(hal, 0, motor_speed_gear(1), (motor_dir_t)99, NULL);
    TEST_ASSERT_FALSE(motor_cmd_ok(r));
    TEST_ASSERT_EQUAL_INT(MOTOR_REJECT_BAD_DIR, r.reject);

    r = motor_exec_run(hal, -1, motor_speed_gear(1), MOTOR_DIR_FORWARD, NULL);
    TEST_ASSERT_FALSE(motor_cmd_ok(r));
    TEST_ASSERT_EQUAL_INT(MOTOR_REJECT_BAD_MOTOR, r.reject);

    r = motor_exec_run(hal, MOTOR_MAX_MOTORS, motor_speed_gear(1), MOTOR_DIR_FORWARD, NULL);
    TEST_ASSERT_FALSE(motor_cmd_ok(r));
    TEST_ASSERT_EQUAL_INT(MOTOR_REJECT_BAD_MOTOR, r.reject);

    memset(&spec, 0, sizeof(spec));
    spec.use_position = true;
    spec.target_pos   = 100;
    r                 = motor_exec_run(hal, 0, motor_speed_gear(1), MOTOR_DIR_FORWARD, &spec);
    TEST_ASSERT_FALSE(motor_cmd_ok(r));
    TEST_ASSERT_EQUAL_INT(MOTOR_REJECT_BASELINE, r.reject);

    TEST_ASSERT_EQUAL_INT(resets, s_fx.reset_count);
    TEST_ASSERT_EQUAL_INT(MOTOR_STATE_FAULT, motor_exec_state(hal, 0));
    TEST_ASSERT_EQUAL_INT(MOTOR_FAULT_OVERCURRENT, motor_exec_fault_code(hal, 0));
}

/* TC-13: 未初始化句柄、运行期改配置副本、重复 bind、reinit 保留位图 */
/* 攻击目标: 初始化依赖；INV-05 confirm_faults 运行期只读 */
void test_init_null_mutate_cfg_rebind_reinit(void)
{
    motor_cmd_result_t  r;
    motor_init_result_t ir;
    motor_exec_t       *dup = s_exec;

    r = motor_exec_run(NULL, 0, motor_speed_gear(1), MOTOR_DIR_FORWARD, NULL);
    TEST_ASSERT_FALSE(motor_cmd_ok(r));
    TEST_ASSERT_EQUAL_INT(MOTOR_REJECT_UNAVAILABLE, r.reject);
    TEST_ASSERT_TRUE(motor_exec_fault_requires_confirm(NULL, 0, MOTOR_FAULT_OVERCURRENT));

    enable_overtemp_monitor();
    s_cfg.motors[0].confirm_faults = motor_fault_confirm_bit(MOTOR_FAULT_OVERTEMP);
    rebind_executor();
    TEST_ASSERT_TRUE(motor_exec_fault_requires_confirm(s_exec, 0, MOTOR_FAULT_OVERTEMP));

    s_cfg.motors[0].confirm_faults = 0u;
    TEST_ASSERT_TRUE(motor_exec_fault_requires_confirm(s_exec, 0, MOTOR_FAULT_OVERTEMP));

    dup = s_exec;
    ir  = motor_executor_bind(0U, &s_cfg, &s_ports, &dup);
    TEST_ASSERT_FALSE(ir.ok);
    TEST_ASSERT_TRUE(motor_exec_fault_requires_confirm(s_exec, 0, MOTOR_FAULT_OVERTEMP));

    run_until_overtemp(s_exec);
    ir = motor_executor_reinit(s_exec);
    TEST_ASSERT_TRUE_MESSAGE(ir.ok, ir.error);
    TEST_ASSERT_EQUAL_INT(MOTOR_STATE_STOPPED, motor_exec_state(s_exec, 0));
    TEST_ASSERT_EQUAL_INT(MOTOR_FAULT_NONE, motor_exec_fault_code(s_exec, 0));
    TEST_ASSERT_TRUE(motor_exec_fault_requires_confirm(s_exec, 0, MOTOR_FAULT_OVERTEMP));
}

/* TC-14: set_output 失败进入非 fatal DRIVER_PORT_FATAL，位图 0 也不得内清 */
/* 攻击目标: 硬覆盖；端口致命码不可被可续动放行，fatal 标志则须 reinit */
void test_driver_port_fatal_not_resumable_even_if_bitmap_zero(void)
{
    motor_cmd_result_t r;
    motor_exec_t      *hal = s_exec;
    int                resets;

    TEST_ASSERT_EQUAL_UINT32(0u, s_cfg.motors[0].confirm_faults);
    TEST_ASSERT_TRUE(motor_exec_fault_requires_confirm(hal, 0, MOTOR_FAULT_DRIVER_PORT_FATAL));

    r = motor_exec_run(hal, 0, motor_speed_gear(1), MOTOR_DIR_FORWARD, NULL);
    TEST_ASSERT_TRUE(motor_cmd_ok(r));
    s_fx.set_output_rc = SW_ERR_HW;
    motor_executor_tick(s_exec);
    TEST_ASSERT_EQUAL_INT(MOTOR_STATE_FAULT, motor_exec_state(hal, 0));
    TEST_ASSERT_EQUAL_INT(MOTOR_FAULT_DRIVER_PORT_FATAL, motor_exec_fault_code(hal, 0));

    s_fx.set_output_rc = SW_OK;
    resets             = s_fx.reset_count;
    r                  = motor_exec_run(hal, 0, motor_speed_gear(1), MOTOR_DIR_FORWARD, NULL);
    TEST_ASSERT_FALSE(motor_cmd_ok(r));
    TEST_ASSERT_TRUE((r.reject == MOTOR_REJECT_FAULT) || (r.reject == MOTOR_REJECT_FATAL));
    TEST_ASSERT_EQUAL_INT(resets, s_fx.reset_count);
    TEST_ASSERT_EQUAL_INT(MOTOR_STATE_FAULT, motor_exec_state(hal, 0));

    init_executor();
    hal              = s_exec;
    s_driver.status  = driver_status;
    s_fx.port_status = MOTOR_PORT_OK;
    r                = motor_exec_run(hal, 0, motor_speed_gear(1), MOTOR_DIR_FORWARD, NULL);
    TEST_ASSERT_TRUE(motor_cmd_ok(r));
    motor_executor_tick(s_exec);
    s_fx.port_status = MOTOR_PORT_FATAL;
    motor_executor_tick(s_exec);
    TEST_ASSERT_EQUAL_INT(MOTOR_FAULT_DRIVER_PORT_FATAL, motor_exec_fault_code(hal, 0));
    r = motor_exec_run(hal, 0, motor_speed_gear(1), MOTOR_DIR_FORWARD, NULL);
    TEST_ASSERT_FALSE(motor_cmd_ok(r));
    TEST_ASSERT_EQUAL_INT(MOTOR_REJECT_FATAL, r.reject);
    r = motor_exec_recover(hal, 0, MOTOR_RECOVERY_DRIVER_RESET);
    TEST_ASSERT_FALSE(motor_cmd_ok(r));
    TEST_ASSERT_EQUAL_INT(MOTOR_REJECT_FATAL, r.reject);
}

int main(void)
{
    UNITY_BEGIN();

    WDF_RUN_TEST(
        test_confirm_fault_repeat_run_home_stay_fault, "", "TC-01 需确认 FAULT 重复 run/home 保持故障且不复位");
    WDF_RUN_TEST(test_resumable_fault_home_clears_and_starts, "", "TC-02 可续动 FAULT 上 home 内清并离开故障");
    WDF_RUN_TEST(test_confirm_partial_recover_then_run_rejected, "", "TC-03 恢复步骤颠倒或半步后 run 仍拒绝");
    WDF_RUN_TEST(test_confirm_fault_concurrent_tick_run_query, "", "TC-04 需确认 FAULT 下并发 tick/run/查询不得放行");
    WDF_RUN_TEST(test_resumable_reset_fail_fills_event_slot, "", "TC-05 连续内清失败打满事件槽仍保持该次 FAULT");
    WDF_RUN_TEST(test_resumable_hammer_run_while_current_stays_high, "", "TC-06 电流仍超限时连打 run 每次内清后再故障");
    WDF_RUN_TEST(
        test_reset_callback_nested_run_keeps_consistent_state, "", "TC-07 复位回调嵌套 run 不得 FAULT 与 RUNNING 撕裂");
    WDF_RUN_TEST(
        test_shared_driver_confirm_isolated_from_peer_resume, "", "TC-08 共享驱动需确认轴不因对轴可续动而放行");
    WDF_RUN_TEST(test_hold_overrides_resumable_fault, "", "TC-09 hold/ESTOP 覆盖可续动 FAULT 不得内清");
    WDF_RUN_TEST(test_watchdog_safe_not_resumable, "", "TC-10 看门狗安全态位图为 0 也不得内清");
    WDF_RUN_TEST(test_confirm_bitmap_edges_and_fatal_hard_override, "", "TC-11 位图边界 bind 拒绝且 FATAL 查询硬覆盖");
    WDF_RUN_TEST(test_resumable_bad_params_do_not_inner_clear, "", "TC-12 非法速度方向电机号不得内清可续动 FAULT");
    WDF_RUN_TEST(test_init_null_mutate_cfg_rebind_reinit, "", "TC-13 空句柄、改配置副本、重复 bind、reinit 保位图");
    WDF_RUN_TEST(test_driver_port_fatal_not_resumable_even_if_bitmap_zero,
                 "",
                 "TC-14 DRIVER_PORT_FATAL 位图为 0 也不得按可续动放行");

    return UNITY_END();
}
