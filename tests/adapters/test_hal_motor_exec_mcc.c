/**
 * @file    test_hal_motor_exec_mcc.c
 * @brief   MCC 电机执行器 HAL provider 单元测试。
 */

#include "ports/outbound/hal/motor/hal_motor_exec_port.h"
#include "tests/stubs/mcc/motor_control_core_fake.h"
#include "wdf_test_spec.h"

static motor_executor_t s_exec;

void setUp(void)
{
    mcc_fake_reset();
}

void tearDown(void)
{
}

static void test_run_continuous_maps_speed_direction_and_result(void)
{
    hal_motor_cmd_result_t      result;
    const mcc_fake_last_call_t *last;

    mcc_fake_set_cmd_result(MOTOR_CMD_QUEUED, "cooldown");

    result = hal_motor_run_continuous((hal_motor_exec_t *)&s_exec, 3, hal_motor_speed_gear(2), HAL_MOTOR_DIR_REVERSE);
    last   = mcc_fake_last_call();

    TEST_ASSERT_EQUAL_INT(HAL_MOTOR_CMD_QUEUED, result.status);
    TEST_ASSERT_EQUAL_STRING("cooldown", result.reason);
    TEST_ASSERT_EQUAL_INT(MCC_FAKE_CALL_RUN_CONTINUOUS, last->call);
    TEST_ASSERT_EQUAL_PTR(&s_exec, last->exec);
    TEST_ASSERT_EQUAL_INT(3, last->motor);
    TEST_ASSERT_EQUAL_INT(MOTOR_SPEED_GEAR, last->speed.kind);
    TEST_ASSERT_EQUAL_INT(2, last->speed.value);
    TEST_ASSERT_EQUAL_INT(MOTOR_DIR_REVERSE, last->dir);
}

static void test_move_to_maps_move_spec(void)
{
    hal_motor_move_spec_t spec = {
        .use_limit      = true,
        .limit          = HAL_MOTOR_LIMIT_NEG,
        .use_position   = true,
        .target_pos     = -1234,
        .use_soft_limit = true,
        .use_time       = true,
        .duration_ms    = 5600,
        .max_time_ms    = 7800,
    };
    const mcc_fake_last_call_t *last;

    TEST_ASSERT_EQUAL_INT(
        HAL_MOTOR_CMD_ACCEPTED,
        hal_motor_move_to((hal_motor_exec_t *)&s_exec, 1, hal_motor_speed_freq(2500), HAL_MOTOR_DIR_FORWARD, &spec)
            .status);
    last = mcc_fake_last_call();

    TEST_ASSERT_EQUAL_INT(MCC_FAKE_CALL_MOVE_TO, last->call);
    TEST_ASSERT_TRUE(last->has_spec);
    TEST_ASSERT_EQUAL_INT(MOTOR_SPEED_FREQ, last->speed.kind);
    TEST_ASSERT_EQUAL_INT(2500, last->speed.value);
    TEST_ASSERT_EQUAL_INT(MOTOR_DIR_FORWARD, last->dir);
    TEST_ASSERT_TRUE(last->spec.use_limit);
    TEST_ASSERT_EQUAL_INT(MOTOR_LIMIT_NEG, last->spec.limit);
    TEST_ASSERT_TRUE(last->spec.use_position);
    TEST_ASSERT_EQUAL_INT64(-1234, last->spec.target_pos);
    TEST_ASSERT_TRUE(last->spec.use_soft_limit);
    TEST_ASSERT_TRUE(last->spec.use_time);
    TEST_ASSERT_EQUAL_UINT64(5600, last->spec.duration_ms);
    TEST_ASSERT_EQUAL_UINT64(7800, last->spec.max_time_ms);
}

static void test_move_to_accepts_null_spec(void)
{
    const mcc_fake_last_call_t *last;

    (void)hal_motor_move_to((hal_motor_exec_t *)&s_exec, 1, hal_motor_speed_freq(1000), HAL_MOTOR_DIR_FORWARD, NULL);
    last = mcc_fake_last_call();

    TEST_ASSERT_EQUAL_INT(MCC_FAKE_CALL_MOVE_TO, last->call);
    TEST_ASSERT_FALSE(last->has_spec);
}

static void test_command_helpers_delegate_to_mcc(void)
{
    const mcc_fake_last_call_t *last;

    (void)hal_motor_stop((hal_motor_exec_t *)&s_exec, 2);
    last = mcc_fake_last_call();
    TEST_ASSERT_EQUAL_INT(MCC_FAKE_CALL_STOP, last->call);
    TEST_ASSERT_EQUAL_INT(2, last->motor);

    (void)hal_motor_set_speed((hal_motor_exec_t *)&s_exec, 4, hal_motor_speed_freq(3300), HAL_MOTOR_DIR_REVERSE);
    last = mcc_fake_last_call();
    TEST_ASSERT_EQUAL_INT(MCC_FAKE_CALL_SET_SPEED, last->call);
    TEST_ASSERT_EQUAL_INT(MOTOR_SPEED_FREQ, last->speed.kind);
    TEST_ASSERT_EQUAL_INT(3300, last->speed.value);
    TEST_ASSERT_EQUAL_INT(MOTOR_DIR_REVERSE, last->dir);

    (void)hal_motor_home((hal_motor_exec_t *)&s_exec, 5);
    last = mcc_fake_last_call();
    TEST_ASSERT_EQUAL_INT(MCC_FAKE_CALL_HOME, last->call);
    TEST_ASSERT_EQUAL_INT(5, last->motor);

    (void)hal_motor_recover((hal_motor_exec_t *)&s_exec, 6, HAL_MOTOR_RECOVERY_MODULE_STOP);
    last = mcc_fake_last_call();
    TEST_ASSERT_EQUAL_INT(MCC_FAKE_CALL_RECOVER, last->call);
    TEST_ASSERT_EQUAL_INT(MOTOR_RECOVERY_MODULE_STOP, last->recovery_step);
}

static void test_rejected_result_is_mapped(void)
{
    hal_motor_cmd_result_t result;

    mcc_fake_set_cmd_result(MOTOR_CMD_REJECTED, "fault");

    result = hal_motor_stop((hal_motor_exec_t *)&s_exec, 0);

    TEST_ASSERT_EQUAL_INT(HAL_MOTOR_CMD_REJECTED, result.status);
    TEST_ASSERT_EQUAL_STRING("fault", result.reason);
}

static void test_phase_direction_position_and_fault_are_mapped(void)
{
    static const struct {
        motor_phase_t     mcc;
        hal_motor_phase_t hal;
    } phases[] = {
        {MOTOR_PHASE_STOPPED,       HAL_MOTOR_PHASE_STOPPED      },
        {MOTOR_PHASE_WAITING_START, HAL_MOTOR_PHASE_WAITING_START},
        {MOTOR_PHASE_REVERSAL_WAIT, HAL_MOTOR_PHASE_REVERSAL_WAIT},
        {MOTOR_PHASE_RUNNING,       HAL_MOTOR_PHASE_RUNNING      },
        {MOTOR_PHASE_PAUSED,        HAL_MOTOR_PHASE_PAUSED       },
        {MOTOR_PHASE_DECELERATING,  HAL_MOTOR_PHASE_DECELERATING },
        {MOTOR_PHASE_FAULT,         HAL_MOTOR_PHASE_FAULT        },
        {MOTOR_PHASE_ESTOP,         HAL_MOTOR_PHASE_ESTOP        },
    };
    static const struct {
        motor_fault_code_t     mcc;
        hal_motor_fault_code_t hal;
    } faults[] = {
        {MOTOR_FAULT_NONE,              HAL_MOTOR_FAULT_NONE             },
        {MOTOR_FAULT_OVERCURRENT,       HAL_MOTOR_FAULT_OVERCURRENT      },
        {MOTOR_FAULT_UNDERCURRENT,      HAL_MOTOR_FAULT_UNDERCURRENT     },
        {MOTOR_FAULT_DRIVER_FEEDBACK,   HAL_MOTOR_FAULT_DRIVER_FEEDBACK  },
        {MOTOR_FAULT_OVERTEMP,          HAL_MOTOR_FAULT_OVERTEMP         },
        {MOTOR_FAULT_UNDERVOLTAGE,      HAL_MOTOR_FAULT_UNDERVOLTAGE     },
        {MOTOR_FAULT_PREPARE_FAILED,    HAL_MOTOR_FAULT_PREPARE_FAILED   },
        {MOTOR_FAULT_ENCODER_SIGNAL,    HAL_MOTOR_FAULT_ENCODER_SIGNAL   },
        {MOTOR_FAULT_WATCHDOG,          HAL_MOTOR_FAULT_WATCHDOG         },
        {MOTOR_FAULT_DRIVER_PORT_FATAL, HAL_MOTOR_FAULT_DRIVER_PORT_FATAL},
        {MOTOR_FAULT_SHARED_DRIVER,     HAL_MOTOR_FAULT_SHARED_DRIVER    },
    };
    size_t i;

    for (i = 0; i < sizeof(phases) / sizeof(phases[0]); i++) {
        mcc_fake_set_query(phases[i].mcc, 42, MOTOR_DIR_REVERSE, MOTOR_FAULT_NONE);
        TEST_ASSERT_EQUAL_INT(phases[i].hal, hal_motor_phase((hal_motor_exec_t *)&s_exec, 0));
        TEST_ASSERT_EQUAL_INT64(42, hal_motor_position((hal_motor_exec_t *)&s_exec, 0));
        TEST_ASSERT_EQUAL_INT(HAL_MOTOR_DIR_REVERSE, hal_motor_direction((hal_motor_exec_t *)&s_exec, 0));
    }

    for (i = 0; i < sizeof(faults) / sizeof(faults[0]); i++) {
        mcc_fake_set_query(MOTOR_PHASE_FAULT, 0, MOTOR_DIR_FORWARD, faults[i].mcc);
        TEST_ASSERT_EQUAL_INT(faults[i].hal, hal_motor_fault_code((hal_motor_exec_t *)&s_exec, 0));
    }
}

static void test_pop_event_maps_all_fields(void)
{
    motor_event_t     pushed = {0};
    hal_motor_event_t out;

    pushed.motor      = 2;
    pushed.type       = MOTOR_EVENT_ARRIVED;
    pushed.trigger    = MOTOR_END_LIMIT;
    pushed.has_limit  = true;
    pushed.limit      = MOTOR_LIMIT_ORIGIN;
    pushed.final_pos  = 12345;
    pushed.elapsed_ms = 678U;
    pushed.fault      = MOTOR_FAULT_NONE;
    mcc_fake_push_event(&pushed);

    TEST_ASSERT_TRUE(hal_motor_pop_event((hal_motor_exec_t *)&s_exec, &out));
    TEST_ASSERT_EQUAL_INT(2, out.motor);
    TEST_ASSERT_EQUAL_INT(HAL_MOTOR_EVENT_ARRIVED, out.type);
    TEST_ASSERT_EQUAL_INT(HAL_MOTOR_END_LIMIT, out.trigger);
    TEST_ASSERT_TRUE(out.has_limit);
    TEST_ASSERT_EQUAL_INT(HAL_MOTOR_LIMIT_ORIGIN, out.limit);
    TEST_ASSERT_EQUAL_INT64(12345, out.final_pos);
    TEST_ASSERT_EQUAL_UINT64(678U, out.elapsed_ms);
    TEST_ASSERT_EQUAL_INT(HAL_MOTOR_FAULT_NONE, out.fault);
}

static void test_pop_event_returns_false_when_empty_or_null(void)
{
    hal_motor_event_t out;

    TEST_ASSERT_FALSE(hal_motor_pop_event((hal_motor_exec_t *)&s_exec, &out));
    TEST_ASSERT_FALSE(hal_motor_pop_event((hal_motor_exec_t *)&s_exec, NULL));
}

static void test_pop_event_preserves_fifo_order(void)
{
    motor_event_t     first  = {0};
    motor_event_t     second = {0};
    hal_motor_event_t out;

    first.motor  = 1;
    first.type   = MOTOR_EVENT_STOPPED;
    second.motor = 5;
    second.type  = MOTOR_EVENT_FAULT;
    second.fault = MOTOR_FAULT_OVERCURRENT;
    mcc_fake_push_event(&first);
    mcc_fake_push_event(&second);

    TEST_ASSERT_TRUE(hal_motor_pop_event((hal_motor_exec_t *)&s_exec, &out));
    TEST_ASSERT_EQUAL_INT(1, out.motor);
    TEST_ASSERT_EQUAL_INT(HAL_MOTOR_EVENT_STOPPED, out.type);

    TEST_ASSERT_TRUE(hal_motor_pop_event((hal_motor_exec_t *)&s_exec, &out));
    TEST_ASSERT_EQUAL_INT(5, out.motor);
    TEST_ASSERT_EQUAL_INT(HAL_MOTOR_EVENT_FAULT, out.type);
    TEST_ASSERT_EQUAL_INT(HAL_MOTOR_FAULT_OVERCURRENT, out.fault);

    TEST_ASSERT_FALSE(hal_motor_pop_event((hal_motor_exec_t *)&s_exec, &out));
}

int main(void)
{
    UNITY_BEGIN();

    WDF_RUN_TEST(test_run_continuous_maps_speed_direction_and_result, "", "验证连续运行映射速度、方向和结果");
    WDF_RUN_TEST(test_move_to_maps_move_spec, "", "验证位置移动规格正确映射到 MCC");
    WDF_RUN_TEST(test_move_to_accepts_null_spec, "", "验证位置移动允许空规格参数");
    WDF_RUN_TEST(test_command_helpers_delegate_to_mcc, "", "验证命令辅助函数委托到MCC");
    WDF_RUN_TEST(test_rejected_result_is_mapped, "", "验证 MCC 拒绝结果被正确映射");
    WDF_RUN_TEST(test_phase_direction_position_and_fault_are_mapped, "", "验证阶段、方向、位置和故障状态正确映射");
    WDF_RUN_TEST(test_pop_event_maps_all_fields, "", "验证取出事件映射全部字段");
    WDF_RUN_TEST(test_pop_event_returns_false_when_empty_or_null, "", "验证事件队列为空或参数为空时取出返回 false");
    WDF_RUN_TEST(test_pop_event_preserves_fifo_order, "", "验证取出事件保留FIFO顺序");

    return UNITY_END();
}
