#include "application/use_cases/process_wash_trigger.h"
#include "application/use_cases/query_wash_session_status.h"
#include "tests/test_support.h"
#include "src/application/coordinators/system_context_private.h"

static int verify_power_on_defaults_to_stopped_and_blocks_start(void)
{
    char response_line[512];
    simulated_driver_context_t driver_context;
    system_context_t system_context;
    wash_session_status_view_t wash_session_status_view;
    operation_result_t result;

    test_setup_system_context(&system_context, &driver_context);
    result = test_load_runtime_program_from_fixture(system_context,
        "tests/fixtures/wash_step_control/program_v1_valid.json",
        0);
    TEST_ASSERT(result.ok);

    result = query_wash_session_status_execute(system_context, &wash_session_status_view);
    TEST_ASSERT(result.ok);
    TEST_ASSERT(wash_session_status_view.device_state == DEVICE_STATE_STOPPED);

    result = test_process_command_and_flush(system_context,
        "start wash_step_control_v1",
        response_line,
        sizeof(response_line));
    TEST_ASSERT(!result.ok);
    TEST_ASSERT(strstr(response_line, "device_state_stopped") != 0);
    TEST_ASSERT(system_context_private_runtime(system_context)->device_state == DEVICE_STATE_STOPPED);

    test_release_system_context(system_context);
    return 0;
}

static int verify_homing_success_runs_real_flow_and_returns_to_idle(void)
{
    int final_segment_index;
    simulated_driver_context_t driver_context;
    system_context_t system_context;
    operation_result_t result;

    test_setup_system_context(&system_context, &driver_context);
    result = test_load_runtime_program_from_fixture(system_context,
        "tests/fixtures/wash_step_control/program_v1_valid.json",
        0);
    TEST_ASSERT(result.ok);

    result = test_homing_system_and_flush(system_context);
    TEST_ASSERT(result.ok);
    TEST_ASSERT(driver_context.stop_all_command_count == 1);
    TEST_ASSERT(driver_context.roof_home_command_count == 1);
    TEST_ASSERT(system_context_private_runtime(system_context)->device_state == DEVICE_STATE_IDLE);
    TEST_ASSERT(strcmp(test_latest_reason_code(system_context), "homing_completed") == 0);

    result = test_start_session_and_flush(system_context, "wash_step_control_v1");
    TEST_ASSERT(result.ok);
    TEST_ASSERT(system_context_private_runtime(system_context)->device_state == DEVICE_STATE_RUNNING);

    final_segment_index = system_context_private_runtime(system_context)->program_snapshot.frozen_program.segment_count - 1;
    system_context_private_runtime(system_context)->wash_execution.segment_index = final_segment_index;
    system_context_private_runtime(system_context)->wash_execution.execution_state = EXECUTION_STATE_COMPLETED;
    system_context_private_runtime(system_context)->wash_execution.lifecycle_state = SEGMENT_LIFECYCLE_COMPLETED;

    result = process_wash_runtime_tick(system_context);
    TEST_ASSERT(result.ok);
    TEST_ASSERT(system_context_private_runtime(system_context)->wash_session.session_state == SESSION_STATE_COMPLETED);
    TEST_ASSERT(system_context_private_runtime(system_context)->device_state == DEVICE_STATE_IDLE);

    test_release_system_context(system_context);
    return 0;
}

static int verify_homing_stop_all_failure_moves_device_to_exception(void)
{
    char response_line[512];
    simulated_driver_context_t driver_context;
    system_context_t system_context;
    operation_result_t result;

    test_setup_system_context(&system_context, &driver_context);
    driver_context.stop_all_should_fail = true;

    result = test_process_command_and_flush(system_context,
        "homing",
        response_line,
        sizeof(response_line));
    TEST_ASSERT(!result.ok);
    TEST_ASSERT(strstr(response_line, "detail=stop_all_failed") != 0);
    TEST_ASSERT(driver_context.stop_all_command_count == 0);
    TEST_ASSERT(driver_context.roof_home_command_count == 0);
    TEST_ASSERT(system_context_private_runtime(system_context)->device_state == DEVICE_STATE_EXCEPTION);

    test_release_system_context(system_context);
    return 0;
}

static int verify_homing_roof_home_command_failure_moves_device_to_exception(void)
{
    char response_line[512];
    simulated_driver_context_t driver_context;
    system_context_t system_context;
    operation_result_t result;

    test_setup_system_context(&system_context, &driver_context);
    driver_context.roof_home_command_should_fail = true;

    result = test_process_command_and_flush(system_context,
        "homing",
        response_line,
        sizeof(response_line));
    TEST_ASSERT(!result.ok);
    TEST_ASSERT(strstr(response_line, "detail=roof_home_command_failed") != 0);
    TEST_ASSERT(driver_context.stop_all_command_count == 1);
    TEST_ASSERT(driver_context.roof_home_command_count == 0);
    TEST_ASSERT(system_context_private_runtime(system_context)->device_state == DEVICE_STATE_EXCEPTION);

    test_release_system_context(system_context);
    return 0;
}

static int verify_homing_snapshot_failure_moves_device_to_exception(void)
{
    char response_line[512];
    simulated_driver_context_t driver_context;
    system_context_t system_context;
    operation_result_t result;

    test_setup_system_context(&system_context, &driver_context);
    driver_context.runtime_snapshot_read_should_fail = true;

    result = test_process_command_and_flush(system_context,
        "homing",
        response_line,
        sizeof(response_line));
    TEST_ASSERT(!result.ok);
    TEST_ASSERT(strstr(response_line, "detail=runtime_snapshot_unavailable") != 0);
    TEST_ASSERT(driver_context.stop_all_command_count == 1);
    TEST_ASSERT(driver_context.roof_home_command_count == 1);
    TEST_ASSERT(system_context_private_runtime(system_context)->device_state == DEVICE_STATE_EXCEPTION);

    test_release_system_context(system_context);
    return 0;
}

static int verify_homing_feedback_timeout_moves_device_to_exception(void)
{
    char response_line[512];
    simulated_driver_context_t driver_context;
    system_context_t system_context;
    operation_result_t result;

    test_setup_system_context(&system_context, &driver_context);
    driver_context.roof_home_feedback_available = false;

    result = test_process_command_and_flush(system_context,
        "homing",
        response_line,
        sizeof(response_line));
    TEST_ASSERT(!result.ok);
    TEST_ASSERT(strstr(response_line, "detail=roof_home_feedback_timeout") != 0);
    TEST_ASSERT(driver_context.stop_all_command_count == 1);
    TEST_ASSERT(driver_context.roof_home_command_count == 1);
    TEST_ASSERT(system_context_private_runtime(system_context)->device_state == DEVICE_STATE_EXCEPTION);

    test_release_system_context(system_context);
    return 0;
}

static int verify_homing_not_reached_moves_device_to_exception(void)
{
    char response_line[512];
    simulated_driver_context_t driver_context;
    system_context_t system_context;
    operation_result_t result;

    test_setup_system_context(&system_context, &driver_context);
    driver_context.roof_home_feedback_available = true;
    driver_context.roof_home_reached = false;

    result = test_process_command_and_flush(system_context,
        "homing",
        response_line,
        sizeof(response_line));
    TEST_ASSERT(!result.ok);
    TEST_ASSERT(strstr(response_line, "detail=roof_home_not_reached") != 0);
    TEST_ASSERT(driver_context.stop_all_command_count == 1);
    TEST_ASSERT(driver_context.roof_home_command_count == 1);
    TEST_ASSERT(system_context_private_runtime(system_context)->device_state == DEVICE_STATE_EXCEPTION);

    test_release_system_context(system_context);
    return 0;
}

static int verify_exception_can_recover_back_to_idle(void)
{
    simulated_driver_context_t driver_context;
    system_context_t system_context;
    operation_result_t result;

    test_setup_system_context(&system_context, &driver_context);
    result = test_submit_fault_with_reason(system_context, "E_STOP", "device-exception");
    TEST_ASSERT(result.ok);
    TEST_ASSERT(system_context_private_runtime(system_context)->device_state == DEVICE_STATE_EXCEPTION);
    TEST_ASSERT(system_context_private_runtime(system_context)->global_fault_present);

    result = test_clear_fault_and_flush(system_context);
    TEST_ASSERT(result.ok);
    TEST_ASSERT(system_context_private_runtime(system_context)->device_state == DEVICE_STATE_STOPPED);
    TEST_ASSERT(!system_context_private_runtime(system_context)->global_fault_present);

    result = test_homing_system_and_flush(system_context);
    TEST_ASSERT(result.ok);
    TEST_ASSERT(system_context_private_runtime(system_context)->device_state == DEVICE_STATE_IDLE);

    test_release_system_context(system_context);
    return 0;
}

int main(void)
{
    if (verify_power_on_defaults_to_stopped_and_blocks_start() != 0) {
        return 1;
    }
    if (verify_homing_success_runs_real_flow_and_returns_to_idle() != 0) {
        return 1;
    }
    if (verify_homing_stop_all_failure_moves_device_to_exception() != 0) {
        return 1;
    }
    if (verify_homing_roof_home_command_failure_moves_device_to_exception() != 0) {
        return 1;
    }
    if (verify_homing_snapshot_failure_moves_device_to_exception() != 0) {
        return 1;
    }
    if (verify_homing_feedback_timeout_moves_device_to_exception() != 0) {
        return 1;
    }
    if (verify_homing_not_reached_moves_device_to_exception() != 0) {
        return 1;
    }
    if (verify_exception_can_recover_back_to_idle() != 0) {
        return 1;
    }
    return 0;
}
