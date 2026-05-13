#include "tests/test_support.h"
#include "src/application/coordinators/system_context_private.h"

#include "application/use_cases/query_wash_session_status.h"

static int test_ro_water_close_fallback(void)
{
    operation_result_t result;
    simulated_driver_context_t driver_context;
    system_context_t system_context;

    test_setup_system_context(&system_context, &driver_context);
    driver_context.ro_water_close_feedback_available = false;
    result = test_load_runtime_program_from_fixture(system_context,
        "tests/fixtures/wash_step_control/program_v1_valid.json",
        0);
    TEST_ASSERT(result.ok);
    result = test_start_session_and_flush(system_context, "wash_step_control_v1");
    TEST_ASSERT(result.ok);

    driver_context.runtime_snapshot.position_snapshot.gantry_absolute_mm = 9500;
    driver_context.runtime_snapshot.position_snapshot.tail_reached = true;
    result = test_tick(system_context, 100);
    TEST_ASSERT(result.ok);
    driver_context.runtime_snapshot.actuator_feedback.roof_brush_home_reached = true;
    driver_context.runtime_snapshot.position_snapshot.tail_reached = false;
    result = test_tick(system_context, 100);
    TEST_ASSERT(result.ok);
    result = test_tick(system_context, 100);
    TEST_ASSERT(result.ok);

    driver_context.runtime_snapshot.position_snapshot.gantry_absolute_mm = 1000;
    driver_context.runtime_snapshot.position_snapshot.head_reached = true;
    result = test_tick(system_context, 100);
    TEST_ASSERT(result.ok);
    driver_context.runtime_snapshot.position_snapshot.head_reached = false;
    result = test_tick(system_context, 100);
    TEST_ASSERT(result.ok);
    result = test_tick(system_context, 100);
    TEST_ASSERT(result.ok);
    TEST_ASSERT(strcmp(system_context_private_runtime(system_context)->wash_execution.segment_id, "ro_segment") == 0);

    driver_context.runtime_snapshot.position_snapshot.tail_reached = true;
    result = test_tick(system_context, 100);
    TEST_ASSERT(result.ok);
    TEST_ASSERT(system_context_private_runtime(system_context)->wash_execution.lifecycle_state == SEGMENT_LIFECYCLE_EXITING);
    result = test_tick(system_context, 2500);
    TEST_ASSERT(result.ok);
    TEST_ASSERT(strcmp(system_context_private_runtime(system_context)->wash_execution.segment_id, "dryer_segment") == 0);
    test_release_system_context(system_context);
    return 0;
}

static int test_ro_water_missing_feedback_with_declared_feedback_times_out(void)
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

    driver_context.runtime_snapshot.position_snapshot.gantry_absolute_mm = 9500;
    driver_context.runtime_snapshot.position_snapshot.tail_reached = true;
    result = test_tick(system_context, 100);
    TEST_ASSERT(result.ok);
    driver_context.runtime_snapshot.actuator_feedback.roof_brush_home_reached = true;
    driver_context.runtime_snapshot.position_snapshot.tail_reached = false;
    result = test_tick(system_context, 100);
    TEST_ASSERT(result.ok);
    result = test_tick(system_context, 100);
    TEST_ASSERT(result.ok);

    driver_context.runtime_snapshot.position_snapshot.gantry_absolute_mm = 1000;
    driver_context.runtime_snapshot.position_snapshot.head_reached = true;
    result = test_tick(system_context, 100);
    TEST_ASSERT(result.ok);
    driver_context.runtime_snapshot.position_snapshot.head_reached = false;
    result = test_tick(system_context, 100);
    TEST_ASSERT(result.ok);
    result = test_tick(system_context, 100);
    TEST_ASSERT(result.ok);

    driver_context.runtime_snapshot.position_snapshot.tail_reached = true;
    result = test_tick(system_context, 100);
    TEST_ASSERT(result.ok);
    driver_context.runtime_snapshot.actuator_feedback.ro_water_closed = false;
    result = test_tick(system_context, 4100);
    TEST_ASSERT(result.ok);
    TEST_ASSERT(system_context_private_runtime(system_context)->wash_session.session_state == SESSION_STATE_ABORTED);
    TEST_ASSERT(strcmp(test_latest_reason_code(system_context), "exit_timeout") == 0);
    test_release_system_context(system_context);
    return 0;
}

static int test_follow_loss_recovery_failure_returns_error(void)
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

    driver_context.stop_all_should_fail = true;
    driver_context.runtime_snapshot.actuator_feedback.roof_brush_follow_ok = false;
    result = test_tick(system_context, 100);
    TEST_ASSERT(!result.ok);
    TEST_ASSERT(result.error_code == ERROR_CODE_IO_FAILED);
    TEST_ASSERT(system_context_private_runtime(system_context)->wash_session.session_state == SESSION_STATE_RUNNING);
    TEST_ASSERT(system_context_private_runtime(system_context)->wash_execution.lifecycle_state == SEGMENT_LIFECYCLE_RUNNING);
    test_release_system_context(system_context);
    return 0;
}

static int test_chemical_command_failure_does_not_flip_rule_state(void)
{
    operation_result_t result;
    simulated_driver_context_t driver_context;
    system_context_t system_context;

    test_setup_system_context(&system_context, &driver_context);
    driver_context.chemical_set_command_should_fail = true;
    result = test_load_runtime_program_from_fixture(system_context,
        "tests/fixtures/wash_step_control/program_v1_valid.json",
        0);
    TEST_ASSERT(result.ok);
    result = test_start_session_and_flush(system_context, "wash_step_control_v1");
    TEST_ASSERT(result.ok);

    result = test_tick(system_context, 100);
    TEST_ASSERT(!result.ok);
    TEST_ASSERT(result.error_code == ERROR_CODE_IO_FAILED);
    TEST_ASSERT(!system_context_private_runtime(system_context)->wash_execution.active_conditional_controls[0]);
    TEST_ASSERT(driver_context.chemical_command_count == 0);
    test_release_system_context(system_context);
    return 0;
}

static int test_exit_command_failure_does_not_half_switch_state(void)
{
    operation_result_t result;
    simulated_driver_context_t driver_context;
    system_context_t system_context;

    test_setup_system_context(&system_context, &driver_context);
    driver_context.roof_stop_command_should_fail = true;
    result = test_load_runtime_program_from_fixture(system_context,
        "tests/fixtures/wash_step_control/program_v1_valid.json",
        0);
    TEST_ASSERT(result.ok);
    result = test_start_session_and_flush(system_context, "wash_step_control_v1");
    TEST_ASSERT(result.ok);

    result = test_tick(system_context, 100);
    TEST_ASSERT(result.ok);
    driver_context.runtime_snapshot.position_snapshot.gantry_absolute_mm = 9500;
    driver_context.runtime_snapshot.position_snapshot.tail_reached = true;
    result = test_tick(system_context, 100);
    TEST_ASSERT(!result.ok);
    TEST_ASSERT(result.error_code == ERROR_CODE_IO_FAILED);
    TEST_ASSERT(system_context_private_runtime(system_context)->wash_execution.lifecycle_state == SEGMENT_LIFECYCLE_RUNNING);
    TEST_ASSERT(system_context_private_runtime(system_context)->wait_condition.timeout_policy != WAIT_TIMEOUT_POLICY_EXIT);
    test_release_system_context(system_context);
    return 0;
}

static int test_exception_paths_can_reset_and_release_cleanly(void)
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
    if (test_ro_water_close_fallback() != 0) {
        return 1;
    }
    if (test_ro_water_missing_feedback_with_declared_feedback_times_out() != 0) {
        return 1;
    }
    if (test_follow_loss_recovery_failure_returns_error() != 0) {
        return 1;
    }
    if (test_chemical_command_failure_does_not_flip_rule_state() != 0) {
        return 1;
    }
    if (test_exit_command_failure_does_not_half_switch_state() != 0) {
        return 1;
    }
    if (test_exception_paths_can_reset_and_release_cleanly() != 0) {
        return 1;
    }
    return 0;
}

