/**
 * @file    test_device_control_patterns.c
 * @brief   device_control patterns 单元测试
 */

#include "common/sw_error.h"
#include "domain/device_control/patterns/fluid_path.h"
#include "domain/device_control/patterns/motor_axis.h"
#include "domain/ports/outbound/hal/motor/hal_motor_exec_port.h"
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
static bool                   s_fault_cb_dir_positive;
static hal_motor_fault_code_t s_fault_cb_code;
static int                    s_fault_cb_count;

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
    s_next_result           = cmd_ok();
    s_run_count             = 0;
    s_move_count            = 0;
    s_stop_count            = 0;
    s_speed_count           = 0;
    s_recover_count         = 0;
    s_fault_cb_dir_positive = false;
    s_fault_cb_code         = HAL_MOTOR_FAULT_NONE;
    s_fault_cb_count        = 0;
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

static void on_process_fault(bool is_positive_dir, hal_motor_fault_code_t fault)
{
    s_fault_cb_dir_positive = is_positive_dir;
    s_fault_cb_code         = fault;
    s_fault_cb_count++;
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
    TEST_ASSERT_EQUAL_INT(HAL_MOTOR_DIR_REVERSE, motor_axis_direction(&axis));
    TEST_ASSERT_EQUAL_INT(3, s_motor[1].speed_gear);
    TEST_ASSERT_EQUAL_INT(1, s_run_count);

    TEST_ASSERT_EQUAL_INT(SW_OK, motor_axis_run(&axis, HAL_MOTOR_DIR_REVERSE, hal_motor_speed_gear(3), NULL));
    TEST_ASSERT_EQUAL_INT(1, s_run_count);
    TEST_ASSERT_EQUAL_INT(1, s_speed_count);

    s_motor[1].phase = HAL_MOTOR_PHASE_DECELERATING;
    TEST_ASSERT_EQUAL_INT(MOTOR_AXIS_STATE_STOPPING, motor_axis_state(&axis));
}

static void test_motor_axis_spec_uses_move_to_and_fault_callback(void)
{
    motor_axis_t            axis;
    hal_motor_move_spec_t   spec;
    motion_lifecycle_opts_t opts;
    hal_motor_exec_t       *exec = (hal_motor_exec_t *)s_motor;

    memset(&axis, 0, sizeof(axis));
    memset(&spec, 0, sizeof(spec));
    opts.motion_actuator_id = 0U;
    opts.on_process_fault   = on_process_fault;

    TEST_ASSERT_EQUAL_INT(SW_OK, motor_axis_init(&axis, exec, 0, &opts));
    TEST_ASSERT_EQUAL_INT(SW_OK, motor_axis_run(&axis, HAL_MOTOR_DIR_FORWARD, hal_motor_speed_gear(2), &spec));
    TEST_ASSERT_EQUAL_INT(1, s_move_count);

    s_next_result    = cmd_rejected();
    s_motor[0].fault = HAL_MOTOR_FAULT_OVERCURRENT;
    TEST_ASSERT_EQUAL_INT(SW_ERR_STATE, motor_axis_run(&axis, HAL_MOTOR_DIR_FORWARD, hal_motor_speed_gear(2), NULL));
    TEST_ASSERT_EQUAL_INT(1, s_fault_cb_count);
    TEST_ASSERT_TRUE(s_fault_cb_dir_positive);
    TEST_ASSERT_EQUAL_INT(HAL_MOTOR_FAULT_OVERCURRENT, s_fault_cb_code);
}

static void test_motor_axis_continuous_stop_and_recover(void)
{
    motor_axis_t      axis;
    hal_motor_exec_t *exec = (hal_motor_exec_t *)s_motor;

    memset(&axis, 0, sizeof(axis));
    TEST_ASSERT_EQUAL_INT(SW_ERR_NOT_INIT, motor_axis_run(&axis, HAL_MOTOR_DIR_FORWARD, hal_motor_speed_gear(1), NULL));
    TEST_ASSERT_EQUAL_INT(SW_OK, motor_axis_init(&axis, exec, 2, NULL));
    TEST_ASSERT_EQUAL_INT(SW_OK, motor_axis_run(&axis, HAL_MOTOR_DIR_REVERSE, hal_motor_speed_gear(4), NULL));
    TEST_ASSERT_EQUAL_INT(MOTOR_AXIS_STATE_MOVING, motor_axis_state(&axis));
    TEST_ASSERT_EQUAL_INT(HAL_MOTOR_DIR_REVERSE, s_motor[2].dir);

    TEST_ASSERT_EQUAL_INT(SW_OK, motor_axis_stop(&axis));
    TEST_ASSERT_EQUAL_INT(MOTOR_AXIS_STATE_IDLE, motor_axis_state(&axis));

    s_motor[2].phase = HAL_MOTOR_PHASE_FAULT;
    s_motor[2].fault = HAL_MOTOR_FAULT_DRIVER_FEEDBACK;
    TEST_ASSERT_EQUAL_INT(SW_ERR_STATE, motor_axis_run(&axis, HAL_MOTOR_DIR_FORWARD, hal_motor_speed_gear(1), NULL));
    TEST_ASSERT_EQUAL_INT(HAL_MOTOR_FAULT_DRIVER_FEEDBACK, motor_axis_fault_code(&axis));
    TEST_ASSERT_EQUAL_INT(SW_OK, motor_axis_recover(&axis, HAL_MOTOR_RECOVERY_MODULE_STOP));
    TEST_ASSERT_EQUAL_INT(1, s_recover_count);
    TEST_ASSERT_EQUAL_INT(MOTOR_AXIS_STATE_IDLE, motor_axis_state(&axis));
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
    TEST_ASSERT_EQUAL_INT(SW_OK, motor_axis_run(&axis, HAL_MOTOR_DIR_FORWARD, hal_motor_speed_freq(0), NULL));
    TEST_ASSERT_EQUAL_INT(1, s_stop_count);
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
    WDF_RUN_TEST(test_motor_axis_spec_uses_move_to_and_fault_callback, "", "验证电机轴规格调用位置移动和故障回调");
    WDF_RUN_TEST(test_motor_axis_continuous_stop_and_recover, "", "验证电机轴连续运行停止并恢复");
    WDF_RUN_TEST(test_motor_axis_preserves_frequency_speed, "", "验证电机轴保留频率速度");
    WDF_RUN_TEST(test_fluid_path_reference_counts_shared_pump, "", "验证流体路径对共享水泵进行引用计数");
    WDF_RUN_TEST(test_fluid_path_rejects_invalid_topology_and_unknown_mask, "", "验证流体路径拒绝无效拓扑和未知掩码");
    WDF_RUN_TEST(test_fluid_path_respects_valve_and_pump_delays, "", "验证流体路径遵守阀门和水泵延时");
    WDF_RUN_TEST(test_fluid_path_emergency_off_is_polled, "", "验证轮询处理流体路径紧急关闭");

    return UNITY_END();
}
