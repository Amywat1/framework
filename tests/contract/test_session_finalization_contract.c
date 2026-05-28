#include "application/coordinators/runtime_event_recorder.h"
#include "application/use_cases/process_wash_trigger.h"
#include "tests/test_support.h"
#include "src/application/coordinators/device_runtime_private.h"

static void mark_session_ready_for_completion(void)
{
    int final_segment_index;
    device_runtime_state_t *runtime;

    runtime = device_runtime_private_runtime_mutable();
    final_segment_index = runtime->program_snapshot.frozen_program.segment_count - 1;
    runtime->wash_execution.segment_index = final_segment_index;
    runtime->wash_execution.execution_state = EXECUTION_STATE_COMPLETED;
    runtime->wash_execution.lifecycle_state = SEGMENT_LIFECYCLE_COMPLETED;
    runtime->wash_execution.execution_result = EXECUTION_RESULT_SEGMENT_COMPLETED;
}

static int verify_final_session_result_is_unique_sink(void)
{
    simulated_driver_context_t driver_context;
    operation_result_t result;

    test_setup_system_context( &driver_context);
    result = test_load_runtime_program_from_fixture(
        "tests/fixtures/wash_step_control/program_v1_valid.json",
        0);
    TEST_ASSERT(result.ok);
    result = test_start_session_and_flush( "wash_step_control_v1");
    TEST_ASSERT(result.ok);
    result = test_submit_stop( "contract-stop");
    TEST_ASSERT(result.ok);

    TEST_ASSERT(device_runtime_private_runtime_mutable()->wash_session.session_state == SESSION_STATE_ABORTED);
    TEST_ASSERT(device_runtime_private_runtime_mutable()->wash_session.final_session_result == RESULT_CODE_MANUAL_ABORT);
    TEST_ASSERT(device_runtime_private_runtime_mutable()->wash_execution.execution_result == EXECUTION_RESULT_STOPPED);
    TEST_ASSERT(strcmp(device_runtime_private_runtime_mutable()->last_result_code, "aborted") == 0);
    TEST_ASSERT(strcmp(device_runtime_private_runtime_mutable()->last_reason_code, "contract-stop") == 0);

    runtime_event_recorder_set_latest_result( "accepted", "projection-only");
    TEST_ASSERT(device_runtime_private_runtime_mutable()->wash_session.final_session_result == RESULT_CODE_MANUAL_ABORT);
    TEST_ASSERT(strcmp(device_runtime_private_runtime_mutable()->last_result_code, "accepted") == 0);
    TEST_ASSERT(strcmp(device_runtime_private_runtime_mutable()->last_reason_code, "projection-only") == 0);
    test_release_system_context();
    return 0;
}

static int verify_completed_session_final_result_is_not_overwritten_by_projection(void)
{
    simulated_driver_context_t driver_context;
    operation_result_t result;

    test_setup_system_context( &driver_context);
    result = test_load_runtime_program_from_fixture(
        "tests/fixtures/wash_step_control/program_v1_valid.json",
        0);
    TEST_ASSERT(result.ok);
    result = test_start_session_and_flush( "wash_step_control_v1");
    TEST_ASSERT(result.ok);

    mark_session_ready_for_completion();
    result = process_wash_runtime_tick();
    TEST_ASSERT(result.ok);
    TEST_ASSERT(device_runtime_private_runtime_mutable()->wash_session.session_state == SESSION_STATE_COMPLETED);
    TEST_ASSERT(device_runtime_private_runtime_mutable()->wash_session.final_session_result == RESULT_CODE_SUCCESS);
    TEST_ASSERT(device_runtime_private_runtime_mutable()->device_state == DEVICE_STATE_IDLE);
    TEST_ASSERT(strcmp(device_runtime_private_runtime_mutable()->last_result_code, "completed") == 0);
    TEST_ASSERT(strcmp(device_runtime_private_runtime_mutable()->last_reason_code, "program_finished") == 0);

    runtime_event_recorder_set_latest_result( "accepted", "projection-only");
    TEST_ASSERT(device_runtime_private_runtime_mutable()->wash_session.final_session_result == RESULT_CODE_SUCCESS);
    TEST_ASSERT(strcmp(device_runtime_private_runtime_mutable()->last_result_code, "accepted") == 0);
    TEST_ASSERT(strcmp(device_runtime_private_runtime_mutable()->last_reason_code, "projection-only") == 0);

    test_release_system_context();
    return 0;
}

int main(void)
{
    if (verify_final_session_result_is_unique_sink() != 0) {
        return 1;
    }
    if (verify_completed_session_final_result_is_not_overwritten_by_projection() != 0) {
        return 1;
    }
    return 0;
}

