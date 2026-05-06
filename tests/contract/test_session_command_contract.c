#include "tests/test_support.h"

int main(void)
{
    system_context_t system_context;
    simulated_driver_context_t driver_context;
    operation_result_t result;

    test_setup_system_context(&system_context, &driver_context);
    result = test_start_session(&system_context, "standard_wash");
    TEST_ASSERT(result.ok);
    TEST_ASSERT(system_context.wash_session.session_id[0] != '\0');

    result = test_start_session(&system_context, "standard_wash");
    TEST_ASSERT(!result.ok);
    TEST_ASSERT(strcmp(system_context.last_transition_record.reason_code, "running_session_exists") == 0);

    result = test_submit_feedback(&system_context, "feedback:unknown", "bad-feedback");
    TEST_ASSERT(result.ok);
    TEST_ASSERT(strcmp(system_context.last_transition_record.reason_code, "unmatched_feedback") == 0);

    result = test_submit_feedback(&system_context, "feedback:pre_soak", "shared-key");
    TEST_ASSERT(result.ok);
    result = test_submit_feedback(&system_context, "feedback:wash", "finish-key");
    TEST_ASSERT(result.ok);
    TEST_ASSERT(system_context.wash_session.session_state == SESSION_STATE_COMPLETED);

    result = test_start_session(&system_context, "standard_wash");
    TEST_ASSERT(result.ok);
    result = test_submit_feedback(&system_context, "feedback:pre_soak", "shared-key");
    TEST_ASSERT(result.ok);
    TEST_ASSERT(strcmp(system_context.wash_session.progress_stage_id, "wash") == 0);
    return 0;
}
