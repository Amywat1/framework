#include "application/coordinators/runtime_event_recorder.h"
#include "tests/test_support.h"
#include "src/application/coordinators/system_context_private.h"

static int verify_final_session_result_is_unique_sink(void)
{
    system_context_t system_context;
    simulated_driver_context_t driver_context;
    operation_result_t result;

    test_setup_system_context(&system_context, &driver_context);
    result = test_load_runtime_program_from_fixture(&system_context,
        "tests/fixtures/wash_step_control/program_v1_valid.json",
        0);
    TEST_ASSERT(result.ok);
    result = test_start_session_and_flush(&system_context, "wash_step_control_v1");
    TEST_ASSERT(result.ok);
    result = test_submit_stop(&system_context, "contract-stop");
    TEST_ASSERT(result.ok);

    TEST_ASSERT(system_context.wash_session.session_state == SESSION_STATE_ABORTED);
    TEST_ASSERT(system_context.wash_session.final_session_result == RESULT_CODE_MANUAL_ABORT);
    TEST_ASSERT(system_context.wash_execution.execution_result == EXECUTION_RESULT_STOPPED);
    TEST_ASSERT(strcmp(system_context.last_result_code, "aborted") == 0);
    TEST_ASSERT(strcmp(system_context.last_reason_code, "contract-stop") == 0);

    runtime_event_recorder_set_latest_result(&system_context, "accepted", "projection-only");
    TEST_ASSERT(system_context.wash_session.final_session_result == RESULT_CODE_MANUAL_ABORT);
    TEST_ASSERT(strcmp(system_context.last_result_code, "accepted") == 0);
    TEST_ASSERT(strcmp(system_context.last_reason_code, "projection-only") == 0);
    return 0;
}

int main(void)
{
    return verify_final_session_result_is_unique_sink();
}
