#include "tests/test_support.h"

#include "application/use_cases/query_wash_session_status.h"

int main(void)
{
    system_context_t system_context;
    simulated_driver_context_t driver_context;
    wash_session_status_view_t wash_session_status_view;
    char last_result_code_before[32];
    char last_reason_code_before[64];
    operation_result_t result;

    test_setup_system_context(&system_context, &driver_context);
    result = test_load_runtime_program_from_fixture(&system_context,
        "tests/fixtures/wash_step_control/program_v1_valid.json",
        0);
    TEST_ASSERT(result.ok);
    result = test_start_session(&system_context, "wash_step_control_v1");
    TEST_ASSERT(result.ok);

    result = query_wash_session_status_execute(&system_context, &wash_session_status_view);
    TEST_ASSERT(result.ok);
    TEST_ASSERT(wash_session_status_view.has_active_session);
    TEST_ASSERT(wash_session_status_view.session_state == SESSION_STATE_RUNNING);
    TEST_ASSERT(wash_session_status_view.execution_state == EXECUTION_STATE_RUNNING);
    TEST_ASSERT(strcmp(wash_session_status_view.stage_id, "roof_segment") == 0);
    strncpy(last_result_code_before, system_context.last_result_code, sizeof(last_result_code_before) - 1);
    strncpy(last_reason_code_before, system_context.last_reason_code, sizeof(last_reason_code_before) - 1);
    last_result_code_before[sizeof(last_result_code_before) - 1] = '\0';
    last_reason_code_before[sizeof(last_reason_code_before) - 1] = '\0';

    result = query_wash_session_status_execute(&system_context, &wash_session_status_view);
    TEST_ASSERT(result.ok);
    TEST_ASSERT(strcmp(system_context.last_result_code, last_result_code_before) == 0);
    TEST_ASSERT(strcmp(system_context.last_reason_code, last_reason_code_before) == 0);
    TEST_ASSERT(system_context.pending_trigger_count == 0u);

    result = test_submit_stop(&system_context, "status-stop");
    TEST_ASSERT(result.ok);
    result = query_wash_session_status_execute(&system_context, &wash_session_status_view);
    TEST_ASSERT(result.ok);
    TEST_ASSERT(!wash_session_status_view.has_active_session);
    TEST_ASSERT(strcmp(wash_session_status_view.reason_code, "status-stop") == 0);

    test_setup_system_context(&system_context, &driver_context);
    result = test_submit_fault_with_reason(&system_context, "E_STOP", "idle-fault");
    TEST_ASSERT(result.ok);
    result = query_wash_session_status_execute(&system_context, &wash_session_status_view);
    TEST_ASSERT(result.ok);
    TEST_ASSERT(!wash_session_status_view.has_active_session);
    TEST_ASSERT(wash_session_status_view.global_fault_present);
    TEST_ASSERT(strcmp(wash_session_status_view.reason_code, "global_fault_recorded") == 0);
    TEST_ASSERT(strcmp(wash_session_status_view.global_fault_reason, "idle-fault") == 0);
    return 0;
}
