#include "tests/test_support.h"

static int verify_retry_and_continue_session(void)
{
    system_context_t system_context;
    simulated_driver_context_t driver_context;
    operation_result_t result;

    test_setup_system_context(&system_context, &driver_context);
    result = test_start_session(&system_context, "standard_wash");
    TEST_ASSERT(result.ok);

    system_context.wait_condition.max_retry_count = 1;
    result = test_fire_timeout(&system_context, 1000);
    TEST_ASSERT(result.ok);
    TEST_ASSERT(system_context.wait_condition.current_retry_count == 1);
    TEST_ASSERT(system_context.wash_execution.execution_state == EXECUTION_STATE_RUNNING);
    TEST_ASSERT(strcmp(system_context.last_reason_code, "timeout_retry") == 0);
    TEST_ASSERT(strcmp(system_context.wash_session.progress_stage_id, "pre_soak") == 0);

    system_context.wait_condition.max_retry_count = 1;
    result = test_fire_timeout(&system_context, 1000);
    TEST_ASSERT(result.ok);
    TEST_ASSERT(system_context.wash_session.session_state == SESSION_STATE_RUNNING);
    TEST_ASSERT(strcmp(system_context.wash_session.progress_stage_id, "wash") == 0);
    return 0;
}

static int verify_finish_execution_policy(void)
{
    system_context_t system_context;
    simulated_driver_context_t driver_context;
    operation_result_t result;

    test_setup_system_context(&system_context, &driver_context);
    result = test_start_session(&system_context, "standard_wash");
    TEST_ASSERT(result.ok);

    system_context.wait_condition.max_retry_count = 0;
    system_context.wait_condition.timeout_policy = WAIT_TIMEOUT_POLICY_FINISH_EXECUTION;
    result = test_fire_timeout(&system_context, 1000);
    TEST_ASSERT(result.ok);
    TEST_ASSERT(system_context.wash_session.session_state == SESSION_STATE_COMPLETED);
    TEST_ASSERT(system_context.wash_session.final_session_result == RESULT_CODE_DEGRADED_COMPLETE);
    TEST_ASSERT(system_context.wait_condition.active == false);
    TEST_ASSERT(strcmp(system_context.last_reason_code, "timeout_finish") == 0);
    return 0;
}

static int verify_abort_session_policy(void)
{
    system_context_t system_context;
    simulated_driver_context_t driver_context;
    operation_result_t result;

    test_setup_system_context(&system_context, &driver_context);
    result = test_start_session(&system_context, "standard_wash");
    TEST_ASSERT(result.ok);

    system_context.wait_condition.max_retry_count = 0;
    system_context.wait_condition.timeout_policy = WAIT_TIMEOUT_POLICY_ABORT_SESSION;
    result = test_fire_timeout(&system_context, 1000);
    TEST_ASSERT(result.ok);
    TEST_ASSERT(system_context.wash_session.session_state == SESSION_STATE_ABORTED);
    TEST_ASSERT(system_context.wash_execution.execution_result == EXECUTION_RESULT_TIMED_OUT);
    TEST_ASSERT(system_context.wait_condition.active == false);
    TEST_ASSERT(strcmp(system_context.last_reason_code, "timeout") == 0);
    return 0;
}

int main(void)
{
    if (verify_retry_and_continue_session() != 0) {
        return 1;
    }
    if (verify_finish_execution_policy() != 0) {
        return 1;
    }
    if (verify_abort_session_policy() != 0) {
        return 1;
    }
    return 0;
}
