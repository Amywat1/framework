#include "tests/test_support.h"
#include "src/application/coordinators/system_context_private.h"

#include "application/use_cases/query_wash_session_status.h"

static int test_follow_loss_aborts_session(void)
{
    operation_result_t result;
    simulated_driver_context_t driver_context;
    system_context_t system_context;

    test_setup_system_context(&system_context, &driver_context);
    result = test_load_runtime_program_from_fixture(system_context,
        "tests/fixtures/wash_step_control/program_v1_valid.json",
        0);
    TEST_ASSERT(result.ok);
    result = test_start_session_and_flush(system_context, "wash_step_control_v1");
    TEST_ASSERT(result.ok);

    driver_context.runtime_snapshot.actuator_feedback.roof_brush_follow_ok = false;
    result = test_tick(system_context, 100);
    TEST_ASSERT(result.ok);
    TEST_ASSERT(system_context_private_runtime(system_context)->wash_session.session_state == SESSION_STATE_ABORTED);
    TEST_ASSERT(strcmp(test_latest_reason_code(system_context), "follow_lost") == 0);
    test_release_system_context(system_context);
    return 0;
}

static int test_exit_timeout_aborts_session(void)
{
    operation_result_t result;
    simulated_driver_context_t driver_context;
    system_context_t system_context;

    test_setup_system_context(&system_context, &driver_context);
    result = test_load_runtime_program_from_fixture(system_context,
        "tests/fixtures/wash_step_control/program_v1_valid.json",
        0);
    TEST_ASSERT(result.ok);
    result = test_start_session_and_flush(system_context, "wash_step_control_v1");
    TEST_ASSERT(result.ok);

    driver_context.roof_home_feedback_available = false;
    driver_context.runtime_snapshot.position_snapshot.gantry_absolute_mm = 9500;
    driver_context.runtime_snapshot.position_snapshot.tail_reached = true;
    result = test_tick(system_context, 100);
    TEST_ASSERT(result.ok);
    result = test_tick(system_context, 6000);
    TEST_ASSERT(result.ok);
    TEST_ASSERT(system_context_private_runtime(system_context)->wash_session.session_state == SESSION_STATE_ABORTED);
    TEST_ASSERT(system_context_private_runtime(system_context)->wash_session.final_session_result == RESULT_CODE_EXIT_FAILED);
    TEST_ASSERT(system_context_private_runtime(system_context)->wash_execution.execution_result == EXECUTION_RESULT_EXIT_TIMEOUT);
    TEST_ASSERT(system_context_private_runtime(system_context)->device_state == DEVICE_STATE_EXCEPTION);
    TEST_ASSERT(strcmp(system_context_private_runtime(system_context)->last_result_code, "aborted") == 0);
    TEST_ASSERT(strcmp(system_context_private_runtime(system_context)->last_reason_code, "exit_timeout") == 0);
    TEST_ASSERT(strcmp(test_latest_reason_code(system_context), "exit_timeout") == 0);
    test_release_system_context(system_context);
    return 0;
}

static int test_runtime_snapshot_read_failure_enters_safe_stop(void)
{
    operation_result_t result;
    simulated_driver_context_t driver_context;
    system_context_t system_context;

    test_setup_system_context(&system_context, &driver_context);
    result = test_load_runtime_program_from_fixture(system_context,
        "tests/fixtures/wash_step_control/program_v1_valid.json",
        0);
    TEST_ASSERT(result.ok);
    result = test_start_session_and_flush(system_context, "wash_step_control_v1");
    TEST_ASSERT(result.ok);

    result = test_tick(system_context, 100);
    TEST_ASSERT(result.ok);
    driver_context.runtime_snapshot_read_should_fail = true;
    result = test_tick(system_context, 100);
    TEST_ASSERT(result.ok);
    TEST_ASSERT(driver_context.stop_all_command_count == 2);
    TEST_ASSERT(system_context_private_runtime(system_context)->wash_execution.execution_state == EXECUTION_STATE_ABORTED);
    TEST_ASSERT(system_context_private_runtime(system_context)->wash_session.session_state == SESSION_STATE_ABORTED);
    TEST_ASSERT(strcmp(test_latest_reason_code(system_context), "runtime_snapshot_unavailable") == 0);

    result = system_context_reset(system_context);
    TEST_ASSERT(result.ok);
    simulated_driver_context_init(&driver_context);
    result = test_load_runtime_program_from_fixture(system_context,
        "tests/fixtures/wash_step_control/program_v1_valid.json",
        0);
    TEST_ASSERT(result.ok);
    result = test_start_session_and_flush(system_context, "wash_step_control_v1");
    TEST_ASSERT(result.ok);
    TEST_ASSERT(system_context_private_runtime(system_context)->wash_session.session_state == SESSION_STATE_RUNNING);

    test_release_system_context(system_context);
    result = query_wash_session_status_execute(system_context, &(wash_session_status_view_t){0});
    TEST_ASSERT(!result.ok);
    TEST_ASSERT(result.error_code == ERROR_CODE_INVALID_STATE);
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

