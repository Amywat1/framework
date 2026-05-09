#include "application/use_cases/query_wash_session_status.h"
#include "tests/test_support.h"

static int verify_context_holds_shared_runtime_facts(void)
{
    system_context_t system_context;
    simulated_driver_context_t driver_context;
    wash_session_status_view_t wash_session_status_view;
    operation_result_t result;
    unsigned int pending_trigger_count_before;
    unsigned long current_time_before;

    test_setup_system_context(&system_context, &driver_context);
    result = test_load_runtime_program_from_fixture(&system_context,
        "tests/fixtures/wash_step_control/program_v1_valid.json",
        0);
    TEST_ASSERT(result.ok);
    result = test_start_session(&system_context, "wash_step_control_v1");
    TEST_ASSERT(result.ok);

    pending_trigger_count_before = system_context.pending_trigger_count;
    current_time_before = system_context.current_time_ms;
    result = query_wash_session_status_execute(&system_context, &wash_session_status_view);
    TEST_ASSERT(result.ok);
    TEST_ASSERT(wash_session_status_view.has_active_session);
    TEST_ASSERT(system_context.pending_trigger_count == pending_trigger_count_before);
    TEST_ASSERT(system_context.current_time_ms == current_time_before);
    TEST_ASSERT(system_context.last_result_code[0] != '\0');
    TEST_ASSERT(system_context.last_reason_code[0] != '\0');
    TEST_ASSERT(system_context.wash_session.session_state == SESSION_STATE_RUNNING);
    return 0;
}

int main(void)
{
    return verify_context_holds_shared_runtime_facts();
}
