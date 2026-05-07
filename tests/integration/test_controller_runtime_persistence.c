#include "tests/test_support.h"

int main(void)
{
    char first_session_id[32];
    system_context_t system_context;
    simulated_driver_context_t driver_context;
    operation_result_t result;

    test_setup_system_context(&system_context, &driver_context);
    memset(first_session_id, 0, sizeof(first_session_id));

    result = test_start_session(&system_context, "standard_wash");
    TEST_ASSERT(result.ok);
    strncpy(first_session_id, system_context.wash_session.session_id, sizeof(first_session_id) - 1);

    result = test_submit_feedback(&system_context, "feedback:pre_soak", "runtime-1");
    TEST_ASSERT(result.ok);
    result = test_submit_feedback(&system_context, "feedback:wash", "runtime-2");
    TEST_ASSERT(result.ok);
    TEST_ASSERT(system_context.wash_session.session_state == SESSION_STATE_COMPLETED);

    result = main_loop_run(&system_context);
    TEST_ASSERT(result.ok);
    TEST_ASSERT(system_context.pending_trigger_count == 0);

    result = test_start_session(&system_context, "standard_wash");
    TEST_ASSERT(result.ok);
    TEST_ASSERT(system_context.wash_session.session_state == SESSION_STATE_RUNNING);
    TEST_ASSERT(strcmp(first_session_id, system_context.wash_session.session_id) != 0);
    TEST_ASSERT(system_context.global_fault_present == false);
    TEST_ASSERT(strcmp(system_context.last_result_code, "accepted") == 0);
    return 0;
}
