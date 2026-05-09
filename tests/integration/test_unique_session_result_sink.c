#include "application/use_cases/query_wash_session_status.h"
#include "tests/test_support.h"
#include "src/application/coordinators/system_context_private.h"

static int verify_session_end_has_single_final_sink(void)
{
    system_context_t system_context;
    simulated_driver_context_t driver_context;
    wash_session_status_view_t wash_session_status_view;
    operation_result_t result;

    test_setup_system_context(&system_context, &driver_context);
    result = test_load_runtime_program_from_fixture(&system_context,
        "tests/fixtures/wash_step_control/program_v1_valid.json",
        0);
    TEST_ASSERT(result.ok);
    result = test_start_session_and_flush(&system_context, "wash_step_control_v1");
    TEST_ASSERT(result.ok);
    result = test_submit_stop(&system_context, "integration-stop");
    TEST_ASSERT(result.ok);
    result = query_wash_session_status_execute(&system_context, &wash_session_status_view);
    TEST_ASSERT(result.ok);

    TEST_ASSERT(system_context.wash_session.final_session_result == RESULT_CODE_MANUAL_ABORT);
    TEST_ASSERT(system_context.wash_execution.execution_result == EXECUTION_RESULT_STOPPED);
    TEST_ASSERT(strcmp(system_context.last_result_code, "aborted") == 0);
    TEST_ASSERT(strcmp(wash_session_status_view.reason_code, "integration-stop") == 0);

    result = query_wash_session_status_execute(&system_context, &wash_session_status_view);
    TEST_ASSERT(result.ok);
    TEST_ASSERT(system_context.wash_session.final_session_result == RESULT_CODE_MANUAL_ABORT);
    return 0;
}

int main(void)
{
    return verify_session_end_has_single_final_sink();
}
