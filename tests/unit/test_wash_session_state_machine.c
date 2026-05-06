#include "tests/test_support.h"

#include "domain/services/wash_session_state_machine.h"

int main(void)
{
    system_context_t system_context;
    simulated_driver_context_t driver_context;
    operation_result_t result;

    test_setup_system_context(&system_context, &driver_context);
    system_context.current_time_ms = 100;
    result = test_start_session(&system_context, "standard_wash");
    TEST_ASSERT(result.ok);
    TEST_ASSERT(system_context.wash_session.session_state == SESSION_STATE_RUNNING);
    TEST_ASSERT(strcmp(system_context.wash_session.progress_stage_id, "pre_soak") == 0);

    result = test_submit_feedback(&system_context, "feedback:pre_soak", "f1");
    TEST_ASSERT(result.ok);
    TEST_ASSERT(strcmp(system_context.wash_session.progress_stage_id, "wash") == 0);

    result = test_submit_feedback(&system_context, "feedback:wash", "f2");
    TEST_ASSERT(result.ok);
    TEST_ASSERT(system_context.wash_session.session_state == SESSION_STATE_COMPLETED);
    TEST_ASSERT(system_context.wash_session.final_session_result == RESULT_CODE_SUCCESS);
    return 0;
}
