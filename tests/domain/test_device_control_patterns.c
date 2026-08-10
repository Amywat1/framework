/**
 * @file    test_device_control_patterns.c
 * @brief   device_control patterns 单元测试
 */

#include "common/sw_error.h"
#include "common/time_util.h"
#include "domain/device_control/patterns/fluid_path.h"
#include "domain/device_control/patterns/motor_axis.h"
#include "domain/ports/outbound/motor/hal_motor_exec_port.h"
#include "runtime/event_bus/event_bus.h"
#include "wdf_test_spec.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define MOCK_MOTOR_MAX 4

typedef struct {
    hal_motor_phase_t      phase;
    hal_motor_dir_t        dir;
    hal_motor_fault_code_t fault;
    int64_t                position;
    int                    speed_gear;
    hal_motor_speed_kind_t speed_kind;
} mock_motor_t;

static mock_motor_t           s_motor[MOCK_MOTOR_MAX];
static hal_motor_cmd_result_t s_next_result;
static int                    s_run_count;
static int                    s_move_count;
static int                    s_stop_count;
static int                    s_speed_count;
static int                    s_recover_count;
static bool                   s_end_cb_valid;
static actuator_id_t          s_end_cb_id;
static motor_axis_end_result_t s_end_cb_result;
static int                    s_end_cb_count;

#define MOCK_EVENT_CAP 8
static hal_motor_event_t s_events[MOCK_EVENT_CAP];
static int               s_ev_head;
static int               s_ev_count;

static hal_motor_cmd_result_t cmd_ok(void)
{
    hal_motor_cmd_result_t r = {HAL_MOTOR_CMD_ACCEPTED, "ok"};
    return r;
}

static hal_motor_cmd_result_t cmd_rejected(void)
{
    hal_motor_cmd_result_t r = {HAL_MOTOR_CMD_REJECTED, "reject"};
    return r;
}

static void mock_motor_reset(void)
{
    memset(s_motor, 0, sizeof(s_motor));
    s_next_result   = cmd_ok();
    s_run_count     = 0;
    s_move_count    = 0;
    s_stop_count    = 0;
    s_speed_count   = 0;
    s_recover_count = 0;
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

hal_motor_cmd_result_t hal_motor_run_continuous(hal_motor_exec_t *exec,
                                                int               motor,
                                                hal_motor_speed_t spd,
                                                hal_motor_dir_t   dir)
{
    (void)exec;
    s_run_count++;
    if (!motor_index_valid(motor) || !hal_motor_cmd_ok(s_next_result)) {
        return s_next_result;
    }
    if (s_motor[motor].phase == HAL_MOTOR_PHASE_FAULT) {
        return cmd_rejected();
    }
    s_motor[motor].phase      = HAL_MOTOR_PHASE_RUNNING;
    s_motor[motor].dir        = dir;
    s_motor[motor].speed_gear = spd.value;
    s_motor[motor].speed_kind = spd.kind;
    return s_next_result;
}

hal_motor_cmd_result_t hal_motor_move_to(hal_motor_exec_t            *exec,
                                         int                          motor,
                                         hal_motor_speed_t            spd,
                                         hal_motor_dir_t              dir,
                                         const hal_motor_move_spec_t *spec)
{
    (void)spec;
    s_move_count++;
    return hal_motor_run_continuous(exec, motor, spd, dir);
}

hal_motor_cmd_result_t hal_motor_stop(hal_motor_exec_t *exec, int motor)
{
    (void)exec;
    s_stop_count++;
    if (!motor_index_valid(motor) || !hal_motor_cmd_ok(s_next_result)) {
        return s_next_result;
    }
    s_motor[motor].phase = HAL_MOTOR_PHASE_STOPPED;
    return s_next_result;
}

hal_motor_cmd_result_t hal_motor_set_speed(hal_motor_exec_t *exec,
                                           int               motor,
                                           hal_motor_speed_t spd,
                                           hal_motor_dir_t   dir)
{
    (void)exec;
    s_speed_count++;
    if (!motor_index_valid(motor) || !hal_motor_cmd_ok(s_next_result)) {
        return s_next_result;
    }
    if (s_motor[motor].phase != HAL_MOTOR_PHASE_RUNNING) {
        return cmd_rejected();
    }
    s_motor[motor].dir        = dir;
    s_motor[motor].speed_gear = spd.value;
    s_motor[motor].speed_kind = spd.kind;
    return s_next_result;
}

hal_motor_cmd_result_t hal_motor_home(hal_motor_exec_t *exec, int motor)
{
    (void)exec;
    if (!motor_index_valid(motor) || !hal_motor_cmd_ok(s_next_result)) {
        return s_next_result;
    }
    s_motor[motor].phase    = HAL_MOTOR_PHASE_STOPPED;
    s_motor[motor].position = 0;
    return s_next_result;
}

hal_motor_cmd_result_t hal_motor_recover(hal_motor_exec_t *exec, int motor, hal_motor_recovery_step_t step)
{
    (void)exec;
    (void)step;
    s_recover_count++;
    if (!motor_index_valid(motor) || !hal_motor_cmd_ok(s_next_result)) {
        return s_next_result;
    }
    s_motor[motor].phase = HAL_MOTOR_PHASE_STOPPED;
    s_motor[motor].fault = HAL_MOTOR_FAULT_NONE;
    return s_next_result;
}

hal_motor_phase_t hal_motor_phase(const hal_motor_exec_t *exec, int motor)
{
    (void)exec;
    return motor_index_valid(motor) ? s_motor[motor].phase : HAL_MOTOR_PHASE_FAULT;
}

int64_t hal_motor_position(const hal_motor_exec_t *exec, int motor)
{
    (void)exec;
    return motor_index_valid(motor) ? s_motor[motor].position : 0;
}

hal_motor_dir_t hal_motor_direction(const hal_motor_exec_t *exec, int motor)
{
    (void)exec;
    return motor_index_valid(motor) ? s_motor[motor].dir : HAL_MOTOR_DIR_FORWARD;
}

hal_motor_fault_code_t hal_motor_fault_code(const hal_motor_exec_t *exec, int motor)
{
    (void)exec;
    return motor_index_valid(motor) ? s_motor[motor].fault : HAL_MOTOR_FAULT_NONE;
}

bool hal_motor_pop_event(hal_motor_exec_t *exec, hal_motor_event_t *out)
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

bool hal_motor_pop_event_for(hal_motor_exec_t *exec, int motor, hal_motor_event_t *out)
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

static void mock_push_event(const hal_motor_event_t *ev)
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
    hal_motor_exec_t *exec = (hal_motor_exec_t *)s_motor;

    memset(&axis, 0, sizeof(axis));
    TEST_ASSERT_EQUAL_INT(SW_ERR_NOT_INIT, motor_axis_run(&axis, HAL_MOTOR_DIR_FORWARD, hal_motor_speed_gear(1), NULL));
    TEST_ASSERT_EQUAL_INT(SW_OK, motor_axis_init(&axis, exec, 1, NULL));
    TEST_ASSERT_EQUAL_INT(SW_OK, motor_axis_run(&axis, HAL_MOTOR_DIR_REVERSE, hal_motor_speed_gear(3), NULL));

    TEST_ASSERT_EQUAL_INT(MOTOR_AXIS_STATE_MOVING, motor_axis_state(&axis));
    TEST_ASSERT_EQUAL_INT(HAL_MOTOR_DIR_REVERSE, s_motor[1].dir);
    TEST_ASSERT_EQUAL_INT(3, s_motor[1].speed_gear);
    TEST_ASSERT_EQUAL_INT(1, s_run_count);

    TEST_ASSERT_EQUAL_INT(SW_OK, motor_axis_run(&axis, HAL_MOTOR_DIR_REVERSE, hal_motor_speed_gear(3), NULL));
    TEST_ASSERT_EQUAL_INT(1, s_run_count);
    TEST_ASSERT_EQUAL_INT(1, s_speed_count);

    s_motor[1].phase = HAL_MOTOR_PHASE_DECELERATING;
    TEST_ASSERT_EQUAL_INT(MOTOR_AXIS_STATE_MOVING, motor_axis_state(&axis));
    s_motor[1].phase = HAL_MOTOR_PHASE_WAITING_START;
    TEST_ASSERT_EQUAL_INT(MOTOR_AXIS_STATE_MOVING, motor_axis_state(&axis));
    s_motor[1].phase = HAL_MOTOR_PHASE_REVERSAL_WAIT;
    TEST_ASSERT_EQUAL_INT(MOTOR_AXIS_STATE_MOVING, motor_axis_state(&axis));
}

static void test_motor_axis_spec_uses_move_to_and_end_callback(void)
{
    motor_axis_t            axis;
    hal_motor_move_spec_t   spec;
    motion_lifecycle_opts_t opts;
    motor_axis_end_result_t last;
    hal_motor_exec_t       *exec = (hal_motor_exec_t *)s_motor;

    memset(&axis, 0, sizeof(axis));
    memset(&spec, 0, sizeof(spec));
    opts.motion_actuator_id = 0U;
    opts.on_motion_end      = on_motion_end;

    TEST_ASSERT_EQUAL_INT(SW_OK, motor_axis_init(&axis, exec, 0, &opts));
    TEST_ASSERT_EQUAL_INT(SW_OK, motor_axis_run(&axis, HAL_MOTOR_DIR_FORWARD, hal_motor_speed_gear(2), &spec));
    TEST_ASSERT_EQUAL_INT(1, s_move_count);

    s_next_result    = cmd_rejected();
    s_motor[0].fault = HAL_MOTOR_FAULT_OVERCURRENT;
    TEST_ASSERT_EQUAL_INT(SW_ERR_STATE, motor_axis_run(&axis, HAL_MOTOR_DIR_FORWARD, hal_motor_speed_gear(2), NULL));
    TEST_ASSERT_EQUAL_INT(1, s_end_cb_count);
    TEST_ASSERT_TRUE(s_end_cb_valid);
    TEST_ASSERT_EQUAL_INT(MOTOR_AXIS_OUTCOME_FAULT, s_end_cb_result.outcome);
    TEST_ASSERT_EQUAL_INT(HAL_MOTOR_FAULT_OVERCURRENT, s_end_cb_result.fault);
    last = motor_axis_last_result(&axis);
    TEST_ASSERT_TRUE(last.valid);
    TEST_ASSERT_EQUAL_INT(MOTOR_AXIS_OUTCOME_FAULT, last.outcome);
}

/** @brief 纯命令拒绝（无故障码）不上报故障结局。 */
/** @brief 同故障闩锁下事件与拒令合成只回调一次。 */
static void test_motor_axis_fault_end_dedupes_event_and_reject(void)
{
    motor_axis_t            axis;
    motion_lifecycle_opts_t opts;
    hal_motor_event_t       ev;
    hal_motor_exec_t       *exec = (hal_motor_exec_t *)s_motor;

    memset(&axis, 0, sizeof(axis));
    opts.motion_actuator_id = 0U;
    opts.on_motion_end      = on_motion_end;

    TEST_ASSERT_EQUAL_INT(SW_OK, motor_axis_init(&axis, exec, 0, &opts));

    memset(&ev, 0, sizeof(ev));
    ev.motor   = 0;
    ev.type    = HAL_MOTOR_EVENT_FAULT;
    ev.fault   = HAL_MOTOR_FAULT_OVERCURRENT;
    s_events[0] = ev;
    s_ev_head   = 0;
    s_ev_count  = 1;
    s_motor[0].phase = HAL_MOTOR_PHASE_FAULT;
    s_motor[0].fault = HAL_MOTOR_FAULT_OVERCURRENT;

    motor_axis_poll(&axis);
    TEST_ASSERT_EQUAL_INT(1, s_end_cb_count);
    TEST_ASSERT_EQUAL_INT(MOTOR_AXIS_OUTCOME_FAULT, s_end_cb_result.outcome);

    s_next_result = cmd_rejected();
    TEST_ASSERT_EQUAL_INT(SW_ERR_STATE, motor_axis_run(&axis, HAL_MOTOR_DIR_FORWARD, hal_motor_speed_gear(1), NULL));
    TEST_ASSERT_EQUAL_INT(1, s_end_cb_count);
}

static void test_motor_axis_reject_without_fault_skips_end_callback(void)
{
    motor_axis_t            axis;
    motion_lifecycle_opts_t opts;
    motor_axis_end_result_t last;
    hal_motor_exec_t       *exec = (hal_motor_exec_t *)s_motor;

    memset(&axis, 0, sizeof(axis));
    opts.motion_actuator_id = 0U;
    opts.on_motion_end      = on_motion_end;

    TEST_ASSERT_EQUAL_INT(SW_OK, motor_axis_init(&axis, exec, 0, &opts));
    s_next_result    = cmd_rejected();
    s_motor[0].fault = HAL_MOTOR_FAULT_NONE;
    s_motor[0].phase = HAL_MOTOR_PHASE_STOPPED;
    TEST_ASSERT_EQUAL_INT(SW_ERR_STATE, motor_axis_run(&axis, HAL_MOTOR_DIR_FORWARD, hal_motor_speed_gear(2), NULL));
    TEST_ASSERT_EQUAL_INT(0, s_end_cb_count);
    last = motor_axis_last_result(&axis);
    TEST_ASSERT_FALSE(last.valid);
}

static void test_motor_axis_continuous_stop_and_recover(void)
{
    motor_axis_t            axis;
    motion_lifecycle_opts_t opts;
    hal_motor_exec_t       *exec = (hal_motor_exec_t *)s_motor;

    memset(&axis, 0, sizeof(axis));
    opts.motion_actuator_id = 0U;
    opts.on_motion_end      = on_motion_end;

    TEST_ASSERT_EQUAL_INT(SW_ERR_NOT_INIT, motor_axis_run(&axis, HAL_MOTOR_DIR_FORWARD, hal_motor_speed_gear(1), NULL));
    TEST_ASSERT_EQUAL_INT(SW_OK, motor_axis_init(&axis, exec, 2, &opts));
    TEST_ASSERT_EQUAL_INT(SW_OK, motor_axis_run(&axis, HAL_MOTOR_DIR_REVERSE, hal_motor_speed_gear(4), NULL));
    TEST_ASSERT_EQUAL_INT(MOTOR_AXIS_STATE_MOVING, motor_axis_state(&axis));
    TEST_ASSERT_EQUAL_INT(HAL_MOTOR_DIR_REVERSE, s_motor[2].dir);

    TEST_ASSERT_EQUAL_INT(SW_OK, motor_axis_stop(&axis));
    TEST_ASSERT_EQUAL_INT(MOTOR_AXIS_STATE_IDLE, motor_axis_state(&axis));
    motor_axis_poll(&axis);

    s_motor[2].phase = HAL_MOTOR_PHASE_FAULT;
    s_motor[2].fault = HAL_MOTOR_FAULT_DRIVER_FEEDBACK;
    TEST_ASSERT_EQUAL_INT(SW_ERR_STATE, motor_axis_run(&axis, HAL_MOTOR_DIR_FORWARD, hal_motor_speed_gear(1), NULL));
    TEST_ASSERT_EQUAL_INT(1, s_end_cb_count);
    TEST_ASSERT_EQUAL_INT(HAL_MOTOR_FAULT_DRIVER_FEEDBACK, s_end_cb_result.fault);
    TEST_ASSERT_EQUAL_INT(SW_OK, motor_axis_recover(&axis));
    TEST_ASSERT_EQUAL_INT(2, s_recover_count);
    TEST_ASSERT_EQUAL_INT(MOTOR_AXIS_STATE_IDLE, motor_axis_state(&axis));
    motor_axis_poll(&axis);
}

static void test_motor_axis_preserves_frequency_speed(void)
{
    motor_axis_t      axis;
    hal_motor_exec_t *exec = (hal_motor_exec_t *)s_motor;

    memset(&axis, 0, sizeof(axis));
    TEST_ASSERT_EQUAL_INT(SW_OK, motor_axis_init(&axis, exec, 3, NULL));
    TEST_ASSERT_EQUAL_INT(SW_OK, motor_axis_run(&axis, HAL_MOTOR_DIR_FORWARD, hal_motor_speed_freq(2350), NULL));
    TEST_ASSERT_EQUAL_INT(HAL_MOTOR_SPEED_FREQ, s_motor[3].speed_kind);
    TEST_ASSERT_EQUAL_INT(2350, s_motor[3].speed_gear);

    TEST_ASSERT_EQUAL_INT(SW_ERR_PARAM, motor_axis_run(&axis, HAL_MOTOR_DIR_FORWARD, hal_motor_speed_freq(-1), NULL));
    TEST_ASSERT_EQUAL_INT(SW_ERR_PARAM, motor_axis_run(&axis, HAL_MOTOR_DIR_FORWARD, hal_motor_speed_freq(0), NULL));
    TEST_ASSERT_EQUAL_INT(0, s_stop_count);
}

static void test_motor_axis_poll_publishes_completed_on_idle(void)
{
    motor_axis_t            axis;
    motion_lifecycle_opts_t opts;
    hal_motor_exec_t       *exec = (hal_motor_exec_t *)s_motor;
    event_bus_stats_t       stats;

    memset(&axis, 0, sizeof(axis));
    time_util_init();
    TEST_ASSERT_EQUAL_INT(SW_OK, event_bus_init());

    opts.motion_actuator_id = 9U;
    opts.on_motion_end      = NULL;
    TEST_ASSERT_EQUAL_INT(SW_OK, motor_axis_init(&axis, exec, 0, &opts));
    TEST_ASSERT_EQUAL_INT(SW_OK, motor_axis_run(&axis, HAL_MOTOR_DIR_FORWARD, hal_motor_speed_gear(1), NULL));
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

static void test_motor_axis_poll_skips_waiting_start(void)
{
    motor_axis_t            axis;
    motion_lifecycle_opts_t opts;
    hal_motor_exec_t       *exec = (hal_motor_exec_t *)s_motor;
    event_bus_stats_t       stats;

    memset(&axis, 0, sizeof(axis));
    time_util_init();
    TEST_ASSERT_EQUAL_INT(SW_OK, event_bus_init());

    opts.motion_actuator_id = 11U;
    opts.on_motion_end      = NULL;
    TEST_ASSERT_EQUAL_INT(SW_OK, motor_axis_init(&axis, exec, 0, &opts));
    TEST_ASSERT_EQUAL_INT(SW_OK, motor_axis_run(&axis, HAL_MOTOR_DIR_FORWARD, hal_motor_speed_gear(1), NULL));

    s_motor[0].phase = HAL_MOTOR_PHASE_WAITING_START;
    TEST_ASSERT_EQUAL_INT(MOTOR_AXIS_STATE_MOVING, motor_axis_state(&axis));
    motor_axis_poll(&axis);
    TEST_ASSERT_EQUAL_INT(SW_OK, event_bus_get_stats(&stats));
    TEST_ASSERT_EQUAL_UINT32(0U, stats.published_count);

    s_motor[0].phase = HAL_MOTOR_PHASE_STOPPED;
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
    hal_motor_event_t       ev;
    motor_axis_end_result_t last;
    hal_motor_exec_t       *exec = (hal_motor_exec_t *)s_motor;
    event_bus_stats_t       stats;

    memset(&axis, 0, sizeof(axis));
    time_util_init();
    TEST_ASSERT_EQUAL_INT(SW_OK, event_bus_init());

    opts.motion_actuator_id = 21U;
    opts.on_motion_end      = on_motion_end;
    TEST_ASSERT_EQUAL_INT(SW_OK, motor_axis_init(&axis, exec, 0, &opts));
    TEST_ASSERT_EQUAL_INT(SW_OK, motor_axis_run(&axis, HAL_MOTOR_DIR_FORWARD, hal_motor_speed_gear(1), NULL));

    memset(&ev, 0, sizeof(ev));
    ev.motor     = 1; /* 其它电机，应被保留 */
    ev.type      = HAL_MOTOR_EVENT_ARRIVED;
    ev.trigger   = HAL_MOTOR_END_TIME;
    mock_push_event(&ev);

    memset(&ev, 0, sizeof(ev));
    ev.motor      = 0;
    ev.type       = HAL_MOTOR_EVENT_ARRIVED;
    ev.trigger    = HAL_MOTOR_END_LIMIT;
    ev.has_limit  = true;
    ev.limit      = HAL_MOTOR_LIMIT_ORIGIN;
    ev.final_pos  = 12;
    ev.elapsed_ms = 34;
    mock_push_event(&ev);

    s_motor[0].phase = HAL_MOTOR_PHASE_STOPPED;
    motor_axis_poll(&axis);

    TEST_ASSERT_EQUAL_INT(1, s_end_cb_count);
    TEST_ASSERT_EQUAL_INT(21, (int)s_end_cb_id);
    TEST_ASSERT_EQUAL_INT(MOTOR_AXIS_OUTCOME_ARRIVED, s_end_cb_result.outcome);
    TEST_ASSERT_EQUAL_INT(HAL_MOTOR_END_LIMIT, s_end_cb_result.trigger);
    TEST_ASSERT_TRUE(s_end_cb_result.has_limit);
    TEST_ASSERT_EQUAL_INT(HAL_MOTOR_LIMIT_ORIGIN, s_end_cb_result.limit);
    last = motor_axis_last_result(&axis);
    TEST_ASSERT_EQUAL_INT(HAL_MOTOR_LIMIT_ORIGIN, last.limit);

    TEST_ASSERT_EQUAL_INT(SW_OK, event_bus_get_stats(&stats));
    TEST_ASSERT_EQUAL_UINT32(1U, stats.published_count);

    /* 其它电机事件仍在队列 */
    TEST_ASSERT_TRUE(hal_motor_pop_event(exec, &ev));
    TEST_ASSERT_EQUAL_INT(1, ev.motor);
    TEST_ASSERT_EQUAL_INT(HAL_MOTOR_END_TIME, ev.trigger);

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

static void test_fluid_path_emergency_off_is_polled(void)
{
    uint64_t now_ms = 0U;

    fluid_init_ok();
    TEST_ASSERT_EQUAL_INT(SW_OK, fluid_path_set(FLUID_PATH_MASK(TEST_PATH_A)));
    fluid_drain(&now_ms);
    TEST_ASSERT_TRUE(s_slot_state[TEST_CH_SHARED][FLUID_PATH_SLOT_PUMP]);

    fluid_path_emergency_off();
    fluid_path_poll(now_ms);
    TEST_ASSERT_TRUE(fluid_path_is_settled());
    TEST_ASSERT_FALSE(s_slot_state[TEST_CH_SHARED][FLUID_PATH_SLOT_PUMP]);
    TEST_ASSERT_GREATER_THAN_INT(0, s_all_off_count);
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
    WDF_RUN_TEST(test_motor_axis_poll_skips_waiting_start, "", "验证排队启动态不发布完成事件");
    WDF_RUN_TEST(test_motor_axis_poll_reports_limit_end_then_idle, "", "验证电机轴先上报限位结局再发空闲事件");
    WDF_RUN_TEST(test_fluid_path_reference_counts_shared_pump, "", "验证流体路径对共享水泵进行引用计数");
    WDF_RUN_TEST(test_fluid_path_rejects_invalid_topology_and_unknown_mask, "", "验证流体路径拒绝无效拓扑和未知掩码");
    WDF_RUN_TEST(test_fluid_path_respects_valve_and_pump_delays, "", "验证流体路径遵守阀门和水泵延时");
    WDF_RUN_TEST(test_fluid_path_emergency_off_is_polled, "", "验证轮询处理流体路径紧急关闭");

    return UNITY_END();
}
