#include "tests/test_support.h"

#include "application/use_cases/query_wash_session_status.h"

int main(void)
{
    system_context_t system_context;
    simulated_driver_context_t driver_context;
    wash_session_status_view_t wash_session_status_view;
    operation_result_t result;

    test_setup_system_context(&system_context, &driver_context);
    result = test_start_session(&system_context, "standard_wash");
    TEST_ASSERT(result.ok);

    result = query_wash_session_status_execute(&system_context, &wash_session_status_view);
    TEST_ASSERT(result.ok);
    TEST_ASSERT(wash_session_status_view.session_state == SESSION_STATE_RUNNING);
    TEST_ASSERT(strcmp(wash_session_status_view.stage_id, "pre_soak") == 0);

    result = test_submit_stop(&system_context, "status-stop");
    TEST_ASSERT(result.ok);
    result = query_wash_session_status_execute(&system_context, &wash_session_status_view);
    TEST_ASSERT(result.ok);
    TEST_ASSERT(strcmp(wash_session_status_view.reason_code, "status-stop") == 0);

    test_setup_system_context(&system_context, &driver_context);
    result = acknowledge_fault_execute(&system_context, "E_STOP", "idle-fault");
    TEST_ASSERT(result.ok);
    result = query_wash_session_status_execute(&system_context, &wash_session_status_view);
    TEST_ASSERT(result.ok);
    TEST_ASSERT(!wash_session_status_view.has_active_session);
    TEST_ASSERT(wash_session_status_view.global_fault_present);
    TEST_ASSERT(strcmp(wash_session_status_view.global_fault_reason, "idle-fault") == 0);
    return 0;
}
