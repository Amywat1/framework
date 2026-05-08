#include "tests/test_support.h"

static int test_follow_loss_aborts_session(void)
{
    operation_result_t result;
    simulated_driver_context_t driver_context;
    system_context_t system_context;

    test_setup_system_context(&system_context, &driver_context);
    result = test_load_runtime_program_from_fixture(&system_context,
        "tests/fixtures/wash_step_control/program_v1_valid.json",
        0);
    TEST_ASSERT(result.ok);
    result = test_start_session(&system_context, "wash_step_control_v1");
    TEST_ASSERT(result.ok);

    driver_context.runtime_snapshot.actuator_feedback.roof_brush_follow_ok = false;
    result = test_tick(&system_context, 100);
    TEST_ASSERT(result.ok);
    TEST_ASSERT(system_context.wash_session.session_state == SESSION_STATE_ABORTED);
    TEST_ASSERT(strcmp(test_latest_reason_code(&system_context), "follow_lost") == 0);
    return 0;
}

static int test_exit_timeout_aborts_session(void)
{
    operation_result_t result;
    simulated_driver_context_t driver_context;
    system_context_t system_context;

    test_setup_system_context(&system_context, &driver_context);
    driver_context.roof_home_feedback_available = false;
    result = test_load_runtime_program_from_fixture(&system_context,
        "tests/fixtures/wash_step_control/program_v1_valid.json",
        0);
    TEST_ASSERT(result.ok);
    result = test_start_session(&system_context, "wash_step_control_v1");
    TEST_ASSERT(result.ok);

    driver_context.runtime_snapshot.position_snapshot.gantry_absolute_mm = 9500;
    driver_context.runtime_snapshot.position_snapshot.tail_reached = true;
    result = test_tick(&system_context, 100);
    TEST_ASSERT(result.ok);
    result = test_tick(&system_context, 6000);
    TEST_ASSERT(result.ok);
    TEST_ASSERT(system_context.wash_session.session_state == SESSION_STATE_ABORTED);
    TEST_ASSERT(strcmp(test_latest_reason_code(&system_context), "exit_timeout") == 0);
    return 0;
}

static int test_runtime_snapshot_read_failure_enters_safe_stop(void)
{
    operation_result_t result;
    simulated_driver_context_t driver_context;
    system_context_t system_context;

    test_setup_system_context(&system_context, &driver_context);
    result = test_load_runtime_program_from_fixture(&system_context,
        "tests/fixtures/wash_step_control/program_v1_valid.json",
        0);
    TEST_ASSERT(result.ok);
    result = test_start_session(&system_context, "wash_step_control_v1");
    TEST_ASSERT(result.ok);

    result = test_tick(&system_context, 100);
    TEST_ASSERT(result.ok);
    driver_context.runtime_snapshot_read_should_fail = true;
    result = test_tick(&system_context, 100);
    TEST_ASSERT(result.ok);
    TEST_ASSERT(driver_context.stop_all_command_count == 1);
    TEST_ASSERT(system_context.wash_execution.execution_state == EXECUTION_STATE_ABORTED);
    TEST_ASSERT(system_context.wash_session.session_state == SESSION_STATE_ABORTED);
    TEST_ASSERT(strcmp(test_latest_reason_code(&system_context), "runtime_snapshot_unavailable") == 0);
    return 0;
}

int main(void)
{
    if (test_follow_loss_aborts_session() != 0) {
        return 1;
    }
    if (test_exit_timeout_aborts_session() != 0) {
        return 1;
    }
    if (test_runtime_snapshot_read_failure_enters_safe_stop() != 0) {
        return 1;
    }
    return 0;
}
