#include "application/use_cases/wash_control.h"
#include "application/use_cases/query_wash_session_status.h"
#include "tests/test_support.h"
#include "src/application/coordinators/control_context_private.h"

static void mark_session_ready_for_completion(void)
{
    int final_segment_index;
    wash_execution_t *wash_execution;
    const program_snapshot_t *program_snapshot;

    program_snapshot = control_context_private_program_snapshot();
    wash_execution = control_context_private_wash_execution_mutable();
    final_segment_index = program_snapshot->frozen_program.segment_count - 1;
    wash_execution->segment_index = final_segment_index;
    wash_execution->execution_state = EXECUTION_STATE_COMPLETED;
    wash_execution->lifecycle_state = SEGMENT_LIFECYCLE_COMPLETED;
    wash_execution->execution_result = EXECUTION_RESULT_SEGMENT_COMPLETED;
}

static int verify_session_end_has_single_final_sink(void)
{
    simulated_driver_context_t driver_context;
    wash_session_status_view_t wash_session_status_view;
    operation_result_t result;

    test_setup_control_context( &driver_context);
    result = test_load_program_from_fixture(
        "tests/fixtures/wash_step_control/program_v1_valid.json",
        0);
    TEST_ASSERT(result.ok);
    result = test_start_session_and_flush( "wash_step_control_v1");
    TEST_ASSERT(result.ok);
    result = test_submit_stop( "integration-stop");
    TEST_ASSERT(result.ok);
    result = query_wash_session_status( &wash_session_status_view);
    TEST_ASSERT(result.ok);

    TEST_ASSERT(control_context_private_wash_session()->final_session_result == RESULT_CODE_MANUAL_ABORT);
    TEST_ASSERT(control_context_private_wash_session()->session_state == SESSION_STATE_ABORTED);
    TEST_ASSERT(control_context_private_wash_execution()->execution_result == EXECUTION_RESULT_STOPPED);
    TEST_ASSERT(strcmp(control_context_last_result_code(), "aborted") == 0);
    TEST_ASSERT(strcmp(control_context_last_reason_code(), "integration-stop") == 0);
    TEST_ASSERT(control_context_private_device_state() == DEVICE_STATE_STOPPED);
    TEST_ASSERT(strcmp(wash_session_status_view.reason_code, "integration-stop") == 0);

    result = query_wash_session_status( &wash_session_status_view);
    TEST_ASSERT(result.ok);
    TEST_ASSERT(control_context_private_wash_session()->final_session_result == RESULT_CODE_MANUAL_ABORT);
    test_release_control_context();
    return 0;
}

static int verify_completed_session_uses_success_final_result(void)
{
    simulated_driver_context_t driver_context;
    wash_session_status_view_t wash_session_status_view;
    operation_result_t result;

    test_setup_control_context( &driver_context);
    result = test_load_program_from_fixture(
        "tests/fixtures/wash_step_control/program_v1_valid.json",
        0);
    TEST_ASSERT(result.ok);
    result = test_start_session_and_flush( "wash_step_control_v1");
    TEST_ASSERT(result.ok);

    mark_session_ready_for_completion();
    result = advance_wash_session_program();
    TEST_ASSERT(result.ok);
    result = query_wash_session_status( &wash_session_status_view);
    TEST_ASSERT(result.ok);

    TEST_ASSERT(control_context_private_wash_session()->session_state == SESSION_STATE_COMPLETED);
    TEST_ASSERT(control_context_private_wash_session()->final_session_result == RESULT_CODE_SUCCESS);
    TEST_ASSERT(strcmp(control_context_last_result_code(), "completed") == 0);
    TEST_ASSERT(strcmp(control_context_last_reason_code(), "program_finished") == 0);
    TEST_ASSERT(control_context_private_device_state() == DEVICE_STATE_IDLE);
    TEST_ASSERT(!wash_session_status_view.has_active_session);
    TEST_ASSERT(strcmp(wash_session_status_view.reason_code, "program_finished") == 0);

    test_release_control_context();
    return 0;
}

int main(void)
{
    if (verify_session_end_has_single_final_sink() != 0) {
        return 1;
    }
    if (verify_completed_session_uses_success_final_result() != 0) {
        return 1;
    }
    return 0;
}

