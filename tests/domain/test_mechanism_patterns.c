/**
 * @file    test_mechanism_patterns.c
 * @brief   mechanism patterns 单元测试
 */

#include "application/bridges/mechanism_bridge.h"
#include "common/sw_error.h"
#include "common/time_util.h"
#include "domain/mechanism/motor/motor_executor.h"
#include "domain/mechanism/patterns/fluid_path.h"
#include "domain/mechanism/patterns/motor_axis.h"
#include "domain/ports/outbound/motor/motor_exec_port.h"
#include "runtime/event_bus/event_bus.h"
#include "runtime/scheduler/periodic_task.h"
#include "wdf_test_spec.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define MOCK_MOTOR_MAX 4

typedef struct {
    motor_exec_phase_t      phase;
    motor_dir_t        dir;
    motor_exec_fault_code_t fault;
    int64_t                position;
    int                    speed_gear;
    motor_speed_kind_t speed_kind;
} mock_motor_t;

static mock_motor_t           s_motor[MOCK_MOTOR_MAX];
static motor_cmd_result_t s_next_result;
static int                    s_run_count;
static int                    s_move_count;
static int                    s_stop_count;
static int                    s_home_count;
static int                    s_recover_count;
static int                    s_periodic_register_count;
static int                    s_periodic_fail_on;
static int                    s_executor_tick_count;
static bool                   s_end_cb_valid;
static actuator_id_t          s_end_cb_id;
static motor_axis_end_result_t s_end_cb_result;
static int                    s_end_cb_count;

#define MOCK_EVENT_CAP 8
static motor_event_t s_events[MOCK_EVENT_CAP];
static int               s_ev_head;
static int               s_ev_count;

static motor_cmd_result_t cmd_ok(void)
{
    motor_cmd_result_t r = {
        .status = MOTOR_CMD_ACCEPTED,
        .reject = MOTOR_REJECT_NONE,
        .reason = "ok",
    };
    return r;
}

static motor_cmd_result_t cmd_rejected(void)
{
    motor_cmd_result_t r = {
        .status = MOTOR_CMD_REJECTED,
        .reject = MOTOR_REJECT_FAULT,
        .reason = "reject",
    };
    return r;
}

static void mock_motor_reset(void)
{
    memset(s_motor, 0, sizeof(s_motor));
    s_next_result   = cmd_ok();
    s_run_count     = 0;
    s_move_count    = 0;
    s_stop_count    = 0;
    s_home_count    = 0;
    s_recover_count = 0;
    s_periodic_register_count = 0;
    s_periodic_fail_on        = 0;
    s_executor_tick_count     = 0;
    mechanism_bridge_reset_for_test();
    s_end_cb_valid  = false;
    s_end_cb_id     = 0;
    memset(&s_end_cb_result, 0, sizeof(s_end_cb_result));
    s_end_cb_count = 0;
    s_ev_head      = 0;
    s_ev_count     = 0;
}

static bool motor_index_valid(int motor)
{
    return (motor >= 0) && (motor < MOCK_MOTOR_MAX);
}

motor_cmd_result_t motor_exec_run(motor_exec_t            *exec,
                                     int                          motor,
                                     motor_speed_t            spd,
                                     motor_dir_t              dir,
                                     const motor_move_spec_t *spec)
{
    (void)exec;
    if (spec != NULL) {
        s_move_count++;
    } else {
        s_run_count++;
    }
    if (!motor_index_valid(motor) || !motor_cmd_ok(s_next_result)) {
        return s_next_result;
    }
    if (s_motor[motor].phase == MOTOR_PHASE_FAULT) {
        return cmd_rejected();
    }
    s_motor[motor].phase      = MOTOR_PHASE_RUNNING;
    s_motor[motor].dir        = dir;
    s_motor[motor].speed_gear = spd.value;
    s_motor[motor].speed_kind = spd.kind;
    return s_next_result;
}

motor_cmd_result_t motor_exec_stop(motor_exec_t *exec, int motor)
{
    (void)exec;
    s_stop_count++;
    if (!motor_index_valid(motor) || !motor_cmd_ok(s_next_result)) {
        return s_next_result;
    }
    s_motor[motor].phase = MOTOR_PHASE_STOPPED;
    return s_next_result;
}

motor_cmd_result_t motor_exec_home(motor_exec_t *exec, int motor)
{
    (void)exec;
    s_home_count++;
    if (!motor_index_valid(motor) || !motor_cmd_ok(s_next_result)) {
        return s_next_result;
    }
    s_motor[motor].phase    = MOTOR_PHASE_STOPPED;
    s_motor[motor].position = 0;
    return s_next_result;
}

void motor_executor_tick(motor_exec_t *exec)
{
    (void)exec;
    s_executor_tick_count++;
}

sw_err_t periodic_task_register(const char        *name,
                                uint32_t           period_ms,
                                periodic_task_fn_t fn,
                                void              *ctx,
                                int                sched_policy,
                                int                prio,
                                size_t             stack_size)
{
    (void)name;
    (void)period_ms;
    (void)fn;
    (void)ctx;
    (void)sched_policy;
    (void)prio;
    (void)stack_size;
    s_periodic_register_count++;
    if ((s_periodic_fail_on > 0) && (s_periodic_register_count == s_periodic_fail_on)) {
        return SW_ERR_OVERFLOW;
    }
    return SW_OK;
}

motor_cmd_result_t motor_exec_recover(motor_exec_t *exec, int motor, motor_exec_recovery_step_t step)
{
    (void)exec;
    (void)step;
    s_recover_count++;
    if (!motor_index_valid(motor) || !motor_cmd_ok(s_next_result)) {
        return s_next_result;
    }
    s_motor[motor].phase = MOTOR_PHASE_STOPPED;
    s_motor[motor].fault = MOTOR_FAULT_NONE;
    return s_next_result;
}

motor_exec_phase_t motor_exec_phase(const motor_exec_t *exec, int motor)
{
    (void)exec;
    return motor_index_valid(motor) ? s_motor[motor].phase : MOTOR_PHASE_FAULT;
}

int64_t motor_exec_position(const motor_exec_t *exec, int motor)
{
    (void)exec;
    return motor_index_valid(motor) ? s_motor[motor].position : 0;
}

motor_dir_t motor_exec_direction(const motor_exec_t *exec, int motor)
{
    (void)exec;
    return motor_index_valid(motor) ? s_motor[motor].dir : MOTOR_DIR_FORWARD;
}

motor_exec_fault_code_t motor_exec_fault_code(const motor_exec_t *exec, int motor)
{
    (void)exec;
    return motor_index_valid(motor) ? s_motor[motor].fault : MOTOR_FAULT_NONE;
}

bool motor_exec_encoder_healthy(const motor_exec_t *exec, int motor)
{
    (void)exec;
    return motor_index_valid(motor);
}

bool motor_exec_baseline_trusted(const motor_exec_t *exec, int motor)
{
    (void)exec;
    return motor_index_valid(motor);
}

bool motor_exec_pop_event(motor_exec_t *exec, motor_event_t *out)
{
    (void)exec;
    if ((out == NULL) || (s_ev_count <= 0)) {
        return false;
    }
    *out       = s_events[s_ev_head];
    s_ev_head  = (s_ev_head + 1) % MOCK_EVENT_CAP;
    s_ev_count--;
    return true;
}

bool motor_exec_pop_event_for(motor_exec_t *exec, int motor, motor_event_t *out)
{
    int n;

    (void)exec;
    if ((out == NULL) || (s_ev_count <= 0)) {
        return false;
    }
    for (n = 0; n < s_ev_count; ++n) {
        int idx = (s_ev_head + n) % MOCK_EVENT_CAP;
        int m;

        if (s_events[idx].motor != motor) {
            continue;
        }
        *out = s_events[idx];
        for (m = n; m < s_ev_count - 1; ++m) {
            int from = (s_ev_head + m + 1) % MOCK_EVENT_CAP;
            int to   = (s_ev_head + m) % MOCK_EVENT_CAP;
            s_events[to] = s_events[from];
        }
        s_ev_count--;
        return true;
    }
    return false;
}

static void mock_push_event(const motor_event_t *ev)
{
    int tail;

    if (s_ev_count >= MOCK_EVENT_CAP) {
        return;
    }
    tail           = (s_ev_head + s_ev_count) % MOCK_EVENT_CAP;
    s_events[tail] = *ev;
    s_ev_count++;
}

static void on_motion_end(actuator_id_t id, const motor_axis_end_result_t *result)
{
    s_end_cb_id     = id;
    s_end_cb_result = *result;
    s_end_cb_valid  = result->valid;
    s_end_cb_count++;
}

#define TEST_CH_SHARED 0U
#define TEST_CH_A      1U
#define TEST_CH_B      2U
#define TEST_CH_COUNT  3U
#define TEST_PATH_A    0U
#define TEST_PATH_B    1U

static bool s_slot_state[TEST_CH_COUNT][FLUID_PATH_SLOT_COUNT];
static int  s_all_off_count;

static sw_err_t mock_slot_set(fluid_path_channel_idx_t ch, fluid_path_slot_t slot, bool on)
{
    if (((unsigned)ch >= TEST_CH_COUNT) || ((unsigned)slot >= FLUID_PATH_SLOT_COUNT)) {
        return SW_ERR_PARAM;
    }
    s_slot_state[ch][slot] = on;
    return SW_OK;
}

static sw_err_t mock_all_off(void)
{
    memset(s_slot_state, 0, sizeof(s_slot_state));
    s_all_off_count++;
    return SW_OK;
}

static const fluid_path_actuator_key_t s_path_a_deps[] = {
    {TEST_CH_A,      FLUID_PATH_SLOT_WATER_VALVE},
    {TEST_CH_SHARED, FLUID_PATH_SLOT_PUMP       },
};

static const fluid_path_actuator_key_t s_path_b_deps[] = {
    {TEST_CH_B,      FLUID_PATH_SLOT_WATER_VALVE},
    {TEST_CH_SHARED, FLUID_PATH_SLOT_PUMP       },
};

static const fluid_path_def_t s_paths[] = {
    {TEST_PATH_A, s_path_a_deps, 2U},
    {TEST_PATH_B, s_path_b_deps, 2U},
};

static const fluid_path_cfg_t s_fluid_cfg = {
    .valve_open_delay_ms = 0U,
    .pump_stop_delay_ms  = 0U,
    .channel_count       = TEST_CH_COUNT,
};

static const fluid_path_cfg_t s_fluid_delayed_cfg = {
    .valve_open_delay_ms = 50U,
    .pump_stop_delay_ms  = 40U,
    .channel_count       = TEST_CH_COUNT,
};

static void fluid_init_ok(void)
{
    const fluid_path_actuator_ops_t ops = {
        .slot_set = mock_slot_set,
        .all_off  = mock_all_off,
    };

    TEST_ASSERT_EQUAL_INT(SW_OK, fluid_path_init(&s_fluid_cfg, &ops, s_paths, 2U));
}

static void fluid_drain(uint64_t *now_ms)
{
    unsigned guard = 0U;

    while (!fluid_path_is_settled()) {
        fluid_path_poll(*now_ms);
        *now_ms += 10U;
        guard++;
        TEST_ASSERT_LESS_THAN_UINT(20U, guard);
    }
}

void setUp(void)
{
    uint64_t now_ms = 0U;

    mock_motor_reset();
    memset(s_slot_state, 0, sizeof(s_slot_state));
    s_all_off_count = 0;

    (void)fluid_path_all_off();
    fluid_path_poll(now_ms);
}

void tearDown(void)
{
}

static void test_motor_axis_run_and_query_state(void)
{
    motor_axis_t      axis;
    motor_exec_t *exec = (motor_exec_t *)s_motor;

    memset(&axis, 0, sizeof(axis));
    TEST_ASSERT_EQUAL_INT(SW_ERR_NOT_INIT, motor_axis_run(&axis, MOTOR_DIR_FORWARD, motor_speed_gear(1), NULL));
    TEST_ASSERT_EQUAL_INT(SW_OK, motor_axis_init(&axis, exec, 1, NULL));
    TEST_ASSERT_EQUAL_INT(SW_OK, motor_axis_run(&axis, MOTOR_DIR_REVERSE, motor_speed_gear(3), NULL));

    TEST_ASSERT_EQUAL_INT(MOTOR_AXIS_STATE_MOVING, motor_axis_state(&axis));
    TEST_ASSERT_EQUAL_INT(MOTOR_DIR_REVERSE, s_motor[1].dir);
    TEST_ASSERT_EQUAL_INT(3, s_motor[1].speed_gear);
    TEST_ASSERT_EQUAL_INT(1, s_run_count);

    TEST_ASSERT_EQUAL_INT(SW_OK, motor_axis_run(&axis, MOTOR_DIR_REVERSE, motor_speed_gear(3), NULL));
    TEST_ASSERT_EQUAL_INT(2, s_run_count);

    s_motor[1].phase = MOTOR_PHASE_STOPPING;
    TEST_ASSERT_EQUAL_INT(MOTOR_AXIS_STATE_MOVING, motor_axis_state(&axis));
    s_motor[1].phase = MOTOR_PHASE_WAITING_START;
    TEST_ASSERT_EQUAL_INT(MOTOR_AXIS_STATE_MOVING, motor_axis_state(&axis));
    s_motor[1].phase = MOTOR_PHASE_REVERSAL_WAIT;
    TEST_ASSERT_EQUAL_INT(MOTOR_AXIS_STATE_MOVING, motor_axis_state(&axis));
}

static void test_motor_axis_spec_uses_move_to_and_end_callback(void)
{
    motor_axis_t            axis;
    motor_move_spec_t   spec;
    motion_lifecycle_opts_t opts;
    motor_axis_end_result_t last;
    motor_exec_t       *exec = (motor_exec_t *)s_motor;

    memset(&axis, 0, sizeof(axis));
    memset(&spec, 0, sizeof(spec));
    opts.motion_actuator_id = 0U;
    opts.on_motion_end      = on_motion_end;

    TEST_ASSERT_EQUAL_INT(SW_OK, motor_axis_init(&axis, exec, 0, &opts));
    TEST_ASSERT_EQUAL_INT(SW_OK, motor_axis_run(&axis, MOTOR_DIR_FORWARD, motor_speed_gear(2), &spec));
    TEST_ASSERT_EQUAL_INT(1, s_move_count);

    s_next_result    = cmd_rejected();
    s_motor[0].fault = MOTOR_FAULT_OVERCURRENT;
    TEST_ASSERT_EQUAL_INT(SW_ERR_STATE, motor_axis_run(&axis, MOTOR_DIR_FORWARD, motor_speed_gear(2), NULL));
    TEST_ASSERT_EQUAL_INT(1, s_end_cb_count);
    TEST_ASSERT_TRUE(s_end_cb_valid);
    TEST_ASSERT_EQUAL_INT(MOTOR_EVENT_FAULT, s_end_cb_result.outcome);
    TEST_ASSERT_EQUAL_INT(MOTOR_FAULT_OVERCURRENT, s_end_cb_result.fault);
    last = motor_axis_last_result(&axis);
    TEST_ASSERT_TRUE(last.valid);
    TEST_ASSERT_EQUAL_INT(MOTOR_EVENT_FAULT, last.outcome);
}

/** @brief 纯命令拒绝（无故障码）不上报故障结局。 */
/** @brief 同故障闩锁下事件与拒令合成只回调一次。 */
static void test_motor_axis_fault_end_dedupes_event_and_reject(void)
{
    motor_axis_t            axis;
    motion_lifecycle_opts_t opts;
    motor_event_t       ev;
    motor_exec_t       *exec = (motor_exec_t *)s_motor;

    memset(&axis, 0, sizeof(axis));
    opts.motion_actuator_id = 0U;
    opts.on_motion_end      = on_motion_end;

    TEST_ASSERT_EQUAL_INT(SW_OK, motor_axis_init(&axis, exec, 0, &opts));

    memset(&ev, 0, sizeof(ev));
    ev.motor   = 0;
    ev.type    = MOTOR_EVENT_FAULT;
    ev.fault   = MOTOR_FAULT_OVERCURRENT;
    s_events[0] = ev;
    s_ev_head   = 0;
    s_ev_count  = 1;
    s_motor[0].phase = MOTOR_PHASE_FAULT;
    s_motor[0].fault = MOTOR_FAULT_OVERCURRENT;

    motor_axis_poll(&axis);
    TEST_ASSERT_EQUAL_INT(1, s_end_cb_count);
    TEST_ASSERT_EQUAL_INT(MOTOR_EVENT_FAULT, s_end_cb_result.outcome);

    s_next_result = cmd_rejected();
    TEST_ASSERT_EQUAL_INT(SW_ERR_STATE, motor_axis_run(&axis, MOTOR_DIR_FORWARD, motor_speed_gear(1), NULL));
    TEST_ASSERT_EQUAL_INT(1, s_end_cb_count);
}

static void test_motor_axis_reject_without_fault_skips_end_callback(void)
{
    motor_axis_t            axis;
    motion_lifecycle_opts_t opts;
    motor_axis_end_result_t last;
    motor_exec_t       *exec = (motor_exec_t *)s_motor;

    memset(&axis, 0, sizeof(axis));
    opts.motion_actuator_id = 0U;
    opts.on_motion_end      = on_motion_end;

    TEST_ASSERT_EQUAL_INT(SW_OK, motor_axis_init(&axis, exec, 0, &opts));
    s_next_result    = cmd_rejected();
    s_motor[0].fault = MOTOR_FAULT_NONE;
    s_motor[0].phase = MOTOR_PHASE_STOPPED;
    TEST_ASSERT_EQUAL_INT(SW_ERR_STATE, motor_axis_run(&axis, MOTOR_DIR_FORWARD, motor_speed_gear(2), NULL));
    TEST_ASSERT_EQUAL_INT(0, s_end_cb_count);
    last = motor_axis_last_result(&axis);
    TEST_ASSERT_FALSE(last.valid);
}

static void test_motor_axis_continuous_stop_and_recover(void)
{
    motor_axis_t            axis;
    motion_lifecycle_opts_t opts;
    motor_exec_t       *exec = (motor_exec_t *)s_motor;

    memset(&axis, 0, sizeof(axis));
    opts.motion_actuator_id = 0U;
    opts.on_motion_end      = on_motion_end;

    TEST_ASSERT_EQUAL_INT(SW_ERR_NOT_INIT, motor_axis_run(&axis, MOTOR_DIR_FORWARD, motor_speed_gear(1), NULL));
    TEST_ASSERT_EQUAL_INT(SW_OK, motor_axis_init(&axis, exec, 2, &opts));
    TEST_ASSERT_EQUAL_INT(SW_OK, motor_axis_run(&axis, MOTOR_DIR_REVERSE, motor_speed_gear(4), NULL));
    TEST_ASSERT_EQUAL_INT(MOTOR_AXIS_STATE_MOVING, motor_axis_state(&axis));
    TEST_ASSERT_EQUAL_INT(MOTOR_DIR_REVERSE, s_motor[2].dir);

    TEST_ASSERT_EQUAL_INT(SW_OK, motor_axis_stop(&axis));
    TEST_ASSERT_EQUAL_INT(MOTOR_AXIS_STATE_IDLE, motor_axis_state(&axis));
    motor_axis_poll(&axis);

    s_motor[2].phase = MOTOR_PHASE_FAULT;
    s_motor[2].fault = MOTOR_FAULT_DRIVER_FEEDBACK;
    TEST_ASSERT_EQUAL_INT(SW_ERR_STATE, motor_axis_run(&axis, MOTOR_DIR_FORWARD, motor_speed_gear(1), NULL));
    TEST_ASSERT_EQUAL_INT(1, s_end_cb_count);
    TEST_ASSERT_EQUAL_INT(MOTOR_FAULT_DRIVER_FEEDBACK, s_end_cb_result.fault);
    TEST_ASSERT_EQUAL_INT(SW_OK, motor_axis_recover(&axis));
    TEST_ASSERT_EQUAL_INT(2, s_recover_count);
    TEST_ASSERT_EQUAL_INT(MOTOR_AXIS_STATE_IDLE, motor_axis_state(&axis));
    motor_axis_poll(&axis);
}

static void test_motor_axis_preserves_frequency_speed(void)
{
    motor_axis_t      axis;
    motor_exec_t *exec = (motor_exec_t *)s_motor;

    memset(&axis, 0, sizeof(axis));
    TEST_ASSERT_EQUAL_INT(SW_OK, motor_axis_init(&axis, exec, 3, NULL));
    TEST_ASSERT_EQUAL_INT(SW_OK, motor_axis_run(&axis, MOTOR_DIR_FORWARD, motor_speed_freq(2350), NULL));
    TEST_ASSERT_EQUAL_INT(MOTOR_SPEED_FREQ, s_motor[3].speed_kind);
    TEST_ASSERT_EQUAL_INT(2350, s_motor[3].speed_gear);

    TEST_ASSERT_EQUAL_INT(SW_ERR_PARAM, motor_axis_run(&axis, MOTOR_DIR_FORWARD, motor_speed_freq(-1), NULL));
    TEST_ASSERT_EQUAL_INT(SW_ERR_PARAM, motor_axis_run(&axis, MOTOR_DIR_FORWARD, motor_speed_freq(0), NULL));
    TEST_ASSERT_EQUAL_INT(0, s_stop_count);
}

static void test_motor_axis_poll_publishes_completed_on_idle(void)
{
    motor_axis_t            axis;
    motion_lifecycle_opts_t opts;
    motor_exec_t       *exec = (motor_exec_t *)s_motor;
    event_bus_stats_t       stats;

    memset(&axis, 0, sizeof(axis));
    time_util_init();
    TEST_ASSERT_EQUAL_INT(SW_OK, event_bus_init());

    opts.motion_actuator_id = 9U;
    opts.on_motion_end      = NULL;
    TEST_ASSERT_EQUAL_INT(SW_OK, motor_axis_init(&axis, exec, 0, &opts));
    TEST_ASSERT_EQUAL_INT(SW_OK, motor_axis_run(&axis, MOTOR_DIR_FORWARD, motor_speed_gear(1), NULL));
    motor_axis_poll(&axis);
    TEST_ASSERT_EQUAL_INT(SW_OK, event_bus_get_stats(&stats));
    TEST_ASSERT_EQUAL_UINT32(0U, stats.published_count);

    TEST_ASSERT_EQUAL_INT(SW_OK, motor_axis_stop(&axis));
    motor_axis_poll(&axis);
    TEST_ASSERT_EQUAL_INT(SW_OK, event_bus_get_stats(&stats));
    TEST_ASSERT_EQUAL_UINT32(1U, stats.published_count);

    motor_axis_poll(&axis);
    TEST_ASSERT_EQUAL_INT(SW_OK, event_bus_get_stats(&stats));
    TEST_ASSERT_EQUAL_UINT32(1U, stats.published_count);
    event_bus_shutdown();
}

static void test_motor_axis_is_settled_after_idle_poll(void)
{
    motor_axis_t            axis;
    motion_lifecycle_opts_t opts;
    motor_exec_t       *exec = (motor_exec_t *)s_motor;

    memset(&axis, 0, sizeof(axis));
    time_util_init();
    TEST_ASSERT_EQUAL_INT(SW_OK, event_bus_init());

    TEST_ASSERT_FALSE(motor_axis_is_settled(&axis));
    opts.motion_actuator_id = 13U;
    opts.on_motion_end      = NULL;
    TEST_ASSERT_EQUAL_INT(SW_OK, motor_axis_init(&axis, exec, 0, &opts));
    TEST_ASSERT_TRUE(motor_axis_is_settled(&axis));

    TEST_ASSERT_EQUAL_INT(SW_OK, motor_axis_run(&axis, MOTOR_DIR_FORWARD, motor_speed_gear(1), NULL));
    TEST_ASSERT_FALSE(motor_axis_is_settled(&axis));

    TEST_ASSERT_EQUAL_INT(SW_OK, motor_axis_stop(&axis));
    TEST_ASSERT_FALSE(motor_axis_is_settled(&axis));
    motor_axis_poll(&axis);
    TEST_ASSERT_TRUE(motor_axis_is_settled(&axis));
    event_bus_shutdown();
}

static void test_motor_axis_poll_skips_waiting_start(void)
{
    motor_axis_t            axis;
    motion_lifecycle_opts_t opts;
    motor_exec_t       *exec = (motor_exec_t *)s_motor;
    event_bus_stats_t       stats;

    memset(&axis, 0, sizeof(axis));
    time_util_init();
    TEST_ASSERT_EQUAL_INT(SW_OK, event_bus_init());

    opts.motion_actuator_id = 11U;
    opts.on_motion_end      = NULL;
    TEST_ASSERT_EQUAL_INT(SW_OK, motor_axis_init(&axis, exec, 0, &opts));
    TEST_ASSERT_EQUAL_INT(SW_OK, motor_axis_run(&axis, MOTOR_DIR_FORWARD, motor_speed_gear(1), NULL));

    s_motor[0].phase = MOTOR_PHASE_WAITING_START;
    TEST_ASSERT_EQUAL_INT(MOTOR_AXIS_STATE_MOVING, motor_axis_state(&axis));
    motor_axis_poll(&axis);
    TEST_ASSERT_EQUAL_INT(SW_OK, event_bus_get_stats(&stats));
    TEST_ASSERT_EQUAL_UINT32(0U, stats.published_count);

    s_motor[0].phase = MOTOR_PHASE_STOPPED;
    motor_axis_poll(&axis);
    TEST_ASSERT_EQUAL_INT(SW_OK, event_bus_get_stats(&stats));
    TEST_ASSERT_EQUAL_UINT32(1U, stats.published_count);
    event_bus_shutdown();
}

/** @brief poll 消费本电机事件：先 on_motion_end（含限位种类），再发空闲事件。 */
static void test_motor_axis_poll_reports_limit_end_then_idle(void)
{
    motor_axis_t            axis;
    motion_lifecycle_opts_t opts;
    motor_event_t       ev;
    motor_axis_end_result_t last;
    motor_exec_t       *exec = (motor_exec_t *)s_motor;
    event_bus_stats_t       stats;

    memset(&axis, 0, sizeof(axis));
    time_util_init();
    TEST_ASSERT_EQUAL_INT(SW_OK, event_bus_init());

    opts.motion_actuator_id = 21U;
    opts.on_motion_end      = on_motion_end;
    TEST_ASSERT_EQUAL_INT(SW_OK, motor_axis_init(&axis, exec, 0, &opts));
    TEST_ASSERT_EQUAL_INT(SW_OK, motor_axis_run(&axis, MOTOR_DIR_FORWARD, motor_speed_gear(1), NULL));

    memset(&ev, 0, sizeof(ev));
    ev.motor     = 1; /* 其它电机，应被保留 */
    ev.type      = MOTOR_EVENT_ARRIVED;
    ev.trigger   = MOTOR_END_TIME;
    mock_push_event(&ev);

    memset(&ev, 0, sizeof(ev));
    ev.motor      = 0;
    ev.type       = MOTOR_EVENT_ARRIVED;
    ev.trigger    = MOTOR_END_LIMIT;
    ev.has_limit  = true;
    ev.limit      = MOTOR_LIMIT_ORIGIN;
    ev.final_pos  = 12;
    ev.elapsed_ms = 34;
    mock_push_event(&ev);

    s_motor[0].phase = MOTOR_PHASE_STOPPED;
    motor_axis_poll(&axis);

    TEST_ASSERT_EQUAL_INT(1, s_end_cb_count);
    TEST_ASSERT_EQUAL_INT(21, (int)s_end_cb_id);
    TEST_ASSERT_EQUAL_INT(MOTOR_EVENT_ARRIVED, s_end_cb_result.outcome);
    TEST_ASSERT_EQUAL_INT(MOTOR_END_LIMIT, s_end_cb_result.trigger);
    TEST_ASSERT_TRUE(s_end_cb_result.has_limit);
    TEST_ASSERT_EQUAL_INT(MOTOR_LIMIT_ORIGIN, s_end_cb_result.limit);
    last = motor_axis_last_result(&axis);
    TEST_ASSERT_EQUAL_INT(MOTOR_LIMIT_ORIGIN, last.limit);

    TEST_ASSERT_EQUAL_INT(SW_OK, event_bus_get_stats(&stats));
    TEST_ASSERT_EQUAL_UINT32(1U, stats.published_count);

    /* 其它电机事件仍在队列 */
    TEST_ASSERT_TRUE(motor_exec_pop_event(exec, &ev));
    TEST_ASSERT_EQUAL_INT(1, ev.motor);
    TEST_ASSERT_EQUAL_INT(MOTOR_END_TIME, ev.trigger);

    event_bus_shutdown();
}

static void test_fluid_path_reference_counts_shared_pump(void)
{
    uint64_t now_ms = 0U;

    fluid_init_ok();
    fluid_drain(&now_ms);

    TEST_ASSERT_EQUAL_INT(SW_OK, fluid_path_set(FLUID_PATH_MASK(TEST_PATH_A) | FLUID_PATH_MASK(TEST_PATH_B)));
    fluid_drain(&now_ms);
    TEST_ASSERT_TRUE(s_slot_state[TEST_CH_A][FLUID_PATH_SLOT_WATER_VALVE]);
    TEST_ASSERT_TRUE(s_slot_state[TEST_CH_B][FLUID_PATH_SLOT_WATER_VALVE]);
    TEST_ASSERT_TRUE(s_slot_state[TEST_CH_SHARED][FLUID_PATH_SLOT_PUMP]);

    TEST_ASSERT_EQUAL_INT(SW_OK, fluid_path_set(FLUID_PATH_MASK(TEST_PATH_B)));
    fluid_drain(&now_ms);
    TEST_ASSERT_FALSE(s_slot_state[TEST_CH_A][FLUID_PATH_SLOT_WATER_VALVE]);
    TEST_ASSERT_TRUE(s_slot_state[TEST_CH_B][FLUID_PATH_SLOT_WATER_VALVE]);
    TEST_ASSERT_TRUE(s_slot_state[TEST_CH_SHARED][FLUID_PATH_SLOT_PUMP]);

    TEST_ASSERT_EQUAL_INT(SW_OK, fluid_path_all_off());
    fluid_drain(&now_ms);
    TEST_ASSERT_FALSE(s_slot_state[TEST_CH_SHARED][FLUID_PATH_SLOT_PUMP]);
    TEST_ASSERT_GREATER_THAN_INT(0, s_all_off_count);
}

static void test_fluid_path_rejects_invalid_topology_and_unknown_mask(void)
{
    const fluid_path_actuator_ops_t ops = {
        .slot_set = mock_slot_set,
        .all_off  = mock_all_off,
    };
    const fluid_path_actuator_key_t bad_dep[] = {
        {TEST_CH_COUNT, FLUID_PATH_SLOT_PUMP},
    };
    const fluid_path_def_t duplicate_paths[] = {
        {TEST_PATH_A, s_path_a_deps, 2U},
        {TEST_PATH_A, s_path_b_deps, 2U},
    };
    const fluid_path_def_t bad_paths[] = {
        {TEST_PATH_A, bad_dep, 1U},
    };

    TEST_ASSERT_EQUAL_INT(SW_ERR_PARAM, fluid_path_init(&s_fluid_cfg, &ops, duplicate_paths, 2U));
    TEST_ASSERT_EQUAL_INT(SW_ERR_PARAM, fluid_path_init(&s_fluid_cfg, &ops, bad_paths, 1U));

    fluid_init_ok();
    TEST_ASSERT_EQUAL_INT(SW_ERR_PARAM, fluid_path_set(FLUID_PATH_MASK(7U)));
}

static void test_fluid_path_respects_valve_and_pump_delays(void)
{
    const fluid_path_actuator_ops_t ops = {
        .slot_set = mock_slot_set,
        .all_off  = mock_all_off,
    };

    TEST_ASSERT_EQUAL_INT(SW_OK, fluid_path_init(&s_fluid_delayed_cfg, &ops, s_paths, 2U));
    TEST_ASSERT_EQUAL_INT(SW_OK, fluid_path_set(FLUID_PATH_MASK(TEST_PATH_A)));

    fluid_path_poll(0U);
    TEST_ASSERT_TRUE(s_slot_state[TEST_CH_A][FLUID_PATH_SLOT_WATER_VALVE]);
    TEST_ASSERT_FALSE(s_slot_state[TEST_CH_SHARED][FLUID_PATH_SLOT_PUMP]);

    fluid_path_poll(40U);
    TEST_ASSERT_FALSE(s_slot_state[TEST_CH_SHARED][FLUID_PATH_SLOT_PUMP]);

    fluid_path_poll(50U);
    TEST_ASSERT_TRUE(s_slot_state[TEST_CH_SHARED][FLUID_PATH_SLOT_PUMP]);

    TEST_ASSERT_EQUAL_INT(SW_OK, fluid_path_set(0U));
    fluid_path_poll(50U);
    TEST_ASSERT_FALSE(s_slot_state[TEST_CH_SHARED][FLUID_PATH_SLOT_PUMP]);
    TEST_ASSERT_TRUE(s_slot_state[TEST_CH_A][FLUID_PATH_SLOT_WATER_VALVE]);

    fluid_path_poll(80U);
    TEST_ASSERT_TRUE(s_slot_state[TEST_CH_A][FLUID_PATH_SLOT_WATER_VALVE]);

    fluid_path_poll(90U);
    TEST_ASSERT_FALSE(s_slot_state[TEST_CH_A][FLUID_PATH_SLOT_WATER_VALVE]);
}

static void test_fluid_path_cancels_open_wait_without_starting_pump(void)
{
    const fluid_path_actuator_ops_t ops = {
        .slot_set = mock_slot_set,
        .all_off  = mock_all_off,
    };

    TEST_ASSERT_EQUAL_INT(SW_OK, fluid_path_init(&s_fluid_delayed_cfg, &ops, s_paths, 2U));
    TEST_ASSERT_EQUAL_INT(SW_OK, fluid_path_set(FLUID_PATH_MASK(TEST_PATH_A)));
    fluid_path_poll(0U);
    TEST_ASSERT_TRUE(s_slot_state[TEST_CH_A][FLUID_PATH_SLOT_WATER_VALVE]);
    TEST_ASSERT_FALSE(s_slot_state[TEST_CH_SHARED][FLUID_PATH_SLOT_PUMP]);

    TEST_ASSERT_EQUAL_INT(SW_OK, fluid_path_set(0U));
    fluid_path_poll(10U);
    TEST_ASSERT_FALSE(s_slot_state[TEST_CH_A][FLUID_PATH_SLOT_WATER_VALVE]);
    TEST_ASSERT_FALSE(s_slot_state[TEST_CH_SHARED][FLUID_PATH_SLOT_PUMP]);
    TEST_ASSERT_TRUE(fluid_path_is_settled());
}

static void test_fluid_path_reopens_pump_during_close_wait(void)
{
    const fluid_path_actuator_ops_t ops = {
        .slot_set = mock_slot_set,
        .all_off  = mock_all_off,
    };

    TEST_ASSERT_EQUAL_INT(SW_OK, fluid_path_init(&s_fluid_delayed_cfg, &ops, s_paths, 2U));
    TEST_ASSERT_EQUAL_INT(SW_OK, fluid_path_set(FLUID_PATH_MASK(TEST_PATH_A)));
    fluid_path_poll(0U);
    fluid_path_poll(50U);
    TEST_ASSERT_TRUE(s_slot_state[TEST_CH_SHARED][FLUID_PATH_SLOT_PUMP]);

    TEST_ASSERT_EQUAL_INT(SW_OK, fluid_path_set(0U));
    fluid_path_poll(50U);
    TEST_ASSERT_FALSE(s_slot_state[TEST_CH_SHARED][FLUID_PATH_SLOT_PUMP]);
    TEST_ASSERT_TRUE(s_slot_state[TEST_CH_A][FLUID_PATH_SLOT_WATER_VALVE]);

    TEST_ASSERT_EQUAL_INT(SW_OK, fluid_path_set(FLUID_PATH_MASK(TEST_PATH_A)));
    fluid_path_poll(60U);
    TEST_ASSERT_TRUE(s_slot_state[TEST_CH_SHARED][FLUID_PATH_SLOT_PUMP]);
    TEST_ASSERT_TRUE(s_slot_state[TEST_CH_A][FLUID_PATH_SLOT_WATER_VALVE]);
    TEST_ASSERT_TRUE(fluid_path_is_settled());
}

static void test_fluid_path_open_wait_switch_starts_next_path(void)
{
    const fluid_path_actuator_ops_t ops = {
        .slot_set = mock_slot_set,
        .all_off  = mock_all_off,
    };

    TEST_ASSERT_EQUAL_INT(SW_OK, fluid_path_init(&s_fluid_delayed_cfg, &ops, s_paths, 2U));
    TEST_ASSERT_EQUAL_INT(SW_OK, fluid_path_set(FLUID_PATH_MASK(TEST_PATH_A)));
    fluid_path_poll(0U);
    TEST_ASSERT_TRUE(s_slot_state[TEST_CH_A][FLUID_PATH_SLOT_WATER_VALVE]);

    TEST_ASSERT_EQUAL_INT(SW_OK, fluid_path_set(FLUID_PATH_MASK(TEST_PATH_B)));
    fluid_path_poll(10U);
    TEST_ASSERT_FALSE(s_slot_state[TEST_CH_A][FLUID_PATH_SLOT_WATER_VALVE]);
    TEST_ASSERT_TRUE(s_slot_state[TEST_CH_B][FLUID_PATH_SLOT_WATER_VALVE]);
    TEST_ASSERT_FALSE(s_slot_state[TEST_CH_SHARED][FLUID_PATH_SLOT_PUMP]);
    TEST_ASSERT_FALSE(fluid_path_is_settled());

    fluid_path_poll(60U);
    TEST_ASSERT_TRUE(s_slot_state[TEST_CH_SHARED][FLUID_PATH_SLOT_PUMP]);
    TEST_ASSERT_TRUE(fluid_path_is_settled());
}

static void test_fluid_path_opens_second_valve_during_first_wait(void)
{
    const fluid_path_actuator_ops_t ops = {
        .slot_set = mock_slot_set,
        .all_off  = mock_all_off,
    };

    TEST_ASSERT_EQUAL_INT(SW_OK, fluid_path_init(&s_fluid_delayed_cfg, &ops, s_paths, 2U));
    TEST_ASSERT_EQUAL_INT(SW_OK, fluid_path_set(FLUID_PATH_MASK(TEST_PATH_A)));
    fluid_path_poll(0U);
    TEST_ASSERT_TRUE(s_slot_state[TEST_CH_A][FLUID_PATH_SLOT_WATER_VALVE]);
    TEST_ASSERT_FALSE(s_slot_state[TEST_CH_SHARED][FLUID_PATH_SLOT_PUMP]);

    TEST_ASSERT_EQUAL_INT(SW_OK, fluid_path_enable(FLUID_PATH_MASK(TEST_PATH_B)));
    fluid_path_poll(10U);
    TEST_ASSERT_TRUE(s_slot_state[TEST_CH_A][FLUID_PATH_SLOT_WATER_VALVE]);
    TEST_ASSERT_TRUE(s_slot_state[TEST_CH_B][FLUID_PATH_SLOT_WATER_VALVE]);
    TEST_ASSERT_FALSE(s_slot_state[TEST_CH_SHARED][FLUID_PATH_SLOT_PUMP]);

    fluid_path_poll(50U);
    TEST_ASSERT_FALSE(s_slot_state[TEST_CH_SHARED][FLUID_PATH_SLOT_PUMP]);

    fluid_path_poll(60U);
    TEST_ASSERT_TRUE(s_slot_state[TEST_CH_SHARED][FLUID_PATH_SLOT_PUMP]);
    TEST_ASSERT_TRUE(fluid_path_is_settled());
}

static void test_fluid_path_opens_other_valve_during_pump_stop_wait(void)
{
    const fluid_path_actuator_ops_t ops = {
        .slot_set = mock_slot_set,
        .all_off  = mock_all_off,
    };

    TEST_ASSERT_EQUAL_INT(SW_OK, fluid_path_init(&s_fluid_delayed_cfg, &ops, s_paths, 2U));
    TEST_ASSERT_EQUAL_INT(SW_OK, fluid_path_set(FLUID_PATH_MASK(TEST_PATH_A)));
    fluid_path_poll(0U);
    fluid_path_poll(50U);
    TEST_ASSERT_TRUE(s_slot_state[TEST_CH_SHARED][FLUID_PATH_SLOT_PUMP]);

    TEST_ASSERT_EQUAL_INT(SW_OK, fluid_path_set(0U));
    fluid_path_poll(50U);
    TEST_ASSERT_FALSE(s_slot_state[TEST_CH_SHARED][FLUID_PATH_SLOT_PUMP]);
    TEST_ASSERT_TRUE(s_slot_state[TEST_CH_A][FLUID_PATH_SLOT_WATER_VALVE]);

    TEST_ASSERT_EQUAL_INT(SW_OK, fluid_path_set(FLUID_PATH_MASK(TEST_PATH_B)));
    fluid_path_poll(50U);
    TEST_ASSERT_TRUE(s_slot_state[TEST_CH_B][FLUID_PATH_SLOT_WATER_VALVE]);
    TEST_ASSERT_TRUE(s_slot_state[TEST_CH_A][FLUID_PATH_SLOT_WATER_VALVE]);
    TEST_ASSERT_FALSE(s_slot_state[TEST_CH_SHARED][FLUID_PATH_SLOT_PUMP]);

    fluid_path_poll(90U);
    TEST_ASSERT_FALSE(s_slot_state[TEST_CH_A][FLUID_PATH_SLOT_WATER_VALVE]);
    TEST_ASSERT_TRUE(s_slot_state[TEST_CH_B][FLUID_PATH_SLOT_WATER_VALVE]);
    TEST_ASSERT_FALSE(s_slot_state[TEST_CH_SHARED][FLUID_PATH_SLOT_PUMP]);

    fluid_path_poll(100U);
    TEST_ASSERT_TRUE(s_slot_state[TEST_CH_SHARED][FLUID_PATH_SLOT_PUMP]);
    TEST_ASSERT_TRUE(fluid_path_is_settled());
}

static void test_fluid_path_emergency_off_is_polled(void)
{
    uint64_t now_ms = 0U;

    fluid_init_ok();
    TEST_ASSERT_EQUAL_INT(SW_OK, fluid_path_set(FLUID_PATH_MASK(TEST_PATH_A)));
    fluid_drain(&now_ms);
    TEST_ASSERT_TRUE(s_slot_state[TEST_CH_SHARED][FLUID_PATH_SLOT_PUMP]);

    fluid_path_emergency_off();
    TEST_ASSERT_FALSE(fluid_path_is_settled());
    fluid_path_poll(now_ms);
    TEST_ASSERT_TRUE(fluid_path_is_settled());
    TEST_ASSERT_FALSE(s_slot_state[TEST_CH_SHARED][FLUID_PATH_SLOT_PUMP]);
    TEST_ASSERT_GREATER_THAN_INT(0, s_all_off_count);
}


static void test_motor_axis_home_marks_awaiting_idle(void)
{
    motor_axis_t      axis;
    motor_exec_t *exec = (motor_exec_t *)s_motor;

    memset(&axis, 0, sizeof(axis));
    TEST_ASSERT_EQUAL_INT(SW_ERR_NOT_INIT, motor_axis_home(&axis));
    TEST_ASSERT_EQUAL_INT(SW_OK, motor_axis_init(&axis, exec, 0, NULL));
    TEST_ASSERT_EQUAL_INT(SW_OK, motor_axis_home(&axis));
    TEST_ASSERT_EQUAL_INT(1, s_home_count);
    TEST_ASSERT_FALSE(motor_axis_is_settled(&axis));
}

static void test_mechanism_bridge_returns_same_axis_and_rejects_duplicate(void)
{
    motor_axis_t *axis = NULL;
    motor_axis_t *again = NULL;
    motor_exec_t *exec = (motor_exec_t *)s_motor;

    TEST_ASSERT_EQUAL_INT(SW_ERR_NOT_INIT, mechanism_bridge_add_axis(0, NULL, &axis));
    TEST_ASSERT_EQUAL_INT(SW_OK, mechanism_bridge_bind_motor(exec));
    TEST_ASSERT_EQUAL_INT(SW_ERR_STATE, mechanism_bridge_bind_motor(exec));
    TEST_ASSERT_EQUAL_INT(SW_OK, mechanism_bridge_add_axis(1, NULL, &axis));
    TEST_ASSERT_NOT_NULL(axis);
    TEST_ASSERT_EQUAL_PTR(axis, mechanism_bridge_axis(1));
    TEST_ASSERT_EQUAL_INT(SW_ERR_STATE, mechanism_bridge_add_axis(1, NULL, &again));
    TEST_ASSERT_EQUAL_INT(SW_OK, motor_axis_home(axis));
    TEST_ASSERT_EQUAL_INT(1, s_home_count);
    mechanism_bridge_halt_all();
    TEST_ASSERT_EQUAL_INT(1, s_stop_count);
}

static void test_mechanism_bridge_register_retries_only_failed_task(void)
{
    TEST_ASSERT_EQUAL_INT(SW_OK, mechanism_bridge_bind_motor((motor_exec_t *)s_motor));
    s_periodic_fail_on = 2;
    TEST_ASSERT_EQUAL_INT(SW_ERR_OVERFLOW, mechanism_bridge_register_tasks());
    TEST_ASSERT_EQUAL_INT(2, s_periodic_register_count);
    s_periodic_fail_on = 0;
    TEST_ASSERT_EQUAL_INT(SW_OK, mechanism_bridge_register_tasks());
    TEST_ASSERT_EQUAL_INT(3, s_periodic_register_count);
}

int main(void)
{
    UNITY_BEGIN();

    WDF_RUN_TEST(test_motor_axis_run_and_query_state, "", "验证电机轴运行并查询状态");
    WDF_RUN_TEST(test_motor_axis_spec_uses_move_to_and_end_callback, "", "验证电机轴规格调用到位并上报结局回调");
    WDF_RUN_TEST(test_motor_axis_fault_end_dedupes_event_and_reject,
                 "",
                 "验证故障事件与拒令合成结局去重");
    WDF_RUN_TEST(test_motor_axis_reject_without_fault_skips_end_callback,
                 "",
                 "验证无故障码的命令拒绝不上报结局");
    WDF_RUN_TEST(test_motor_axis_continuous_stop_and_recover, "", "验证电机轴连续运行停止并恢复");
    WDF_RUN_TEST(test_motor_axis_preserves_frequency_speed, "", "验证电机轴保留频率速度");
    WDF_RUN_TEST(test_motor_axis_poll_publishes_completed_on_idle, "", "验证电机轴在空闲边沿发布完成事件");
    WDF_RUN_TEST(test_motor_axis_is_settled_after_idle_poll, "", "验证电机轴运行后未结算、空闲消费后已结算");
    WDF_RUN_TEST(test_motor_axis_home_marks_awaiting_idle, "", "验证电机轴回原后等待空闲");
    WDF_RUN_TEST(test_mechanism_bridge_returns_same_axis_and_rejects_duplicate, "", "验证机构桥接交回轴句柄并拒绝重复登记");
    WDF_RUN_TEST(test_mechanism_bridge_register_retries_only_failed_task, "", "验证机构桥接任务半失败后只补登记失败项");
    WDF_RUN_TEST(test_motor_axis_poll_skips_waiting_start, "", "验证排队启动态不发布完成事件");
    WDF_RUN_TEST(test_motor_axis_poll_reports_limit_end_then_idle, "", "验证电机轴先上报限位结局再发空闲事件");
    WDF_RUN_TEST(test_fluid_path_reference_counts_shared_pump, "", "验证流体路径对共享水泵进行引用计数");
    WDF_RUN_TEST(test_fluid_path_rejects_invalid_topology_and_unknown_mask, "", "验证流体路径拒绝无效拓扑和未知掩码");
    WDF_RUN_TEST(test_fluid_path_respects_valve_and_pump_delays, "", "验证流体路径遵守阀门和水泵延时");
    WDF_RUN_TEST(test_fluid_path_cancels_open_wait_without_starting_pump, "", "验证等开阀期间取消则不开泵并立刻关阀");
    WDF_RUN_TEST(test_fluid_path_reopens_pump_during_close_wait, "", "验证等关泵期间重新开启则立刻开泵");
    WDF_RUN_TEST(test_fluid_path_open_wait_switch_starts_next_path, "", "验证等开阀期间改开其它路径同一拍切换");
    WDF_RUN_TEST(test_fluid_path_opens_second_valve_during_first_wait, "", "验证等开阀期间可立刻开启另一条路径的阀");
    WDF_RUN_TEST(test_fluid_path_opens_other_valve_during_pump_stop_wait, "", "验证等关泵期间可立刻开启另一条路径的阀");
    WDF_RUN_TEST(test_fluid_path_emergency_off_is_polled, "", "验证轮询处理流体路径紧急关闭");

    return UNITY_END();
}
