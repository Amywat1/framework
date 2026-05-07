#include "tests/test_support.h"

static int verify_stop_path(void)
{
    system_context_t system_context;
    simulated_driver_context_t driver_context;
    operation_result_t result;

    test_setup_system_context(&system_context, &driver_context);
    result = test_start_session(&system_context, "standard_wash");
    TEST_ASSERT(result.ok);
    result = test_submit_stop(&system_context, "integration-stop");
    TEST_ASSERT(result.ok);
    TEST_ASSERT(system_context.wash_session.session_state == SESSION_STATE_ABORTED);
    return 0;
}

static int verify_fault_path(void)
{
    system_context_t system_context;
    simulated_driver_context_t driver_context;
    operation_result_t result;

    test_setup_system_context(&system_context, &driver_context);
    result = test_start_session(&system_context, "standard_wash");
    TEST_ASSERT(result.ok);
    result = test_submit_fault(&system_context, "fault-e-stop");
    TEST_ASSERT(result.ok);
    TEST_ASSERT(system_context.wash_execution.execution_result == EXECUTION_RESULT_FAULTED);
    return 0;
}

static int verify_timeout_path(void)
{
    system_context_t system_context;
    simulated_driver_context_t driver_context;
    operation_result_t result;

    test_setup_system_context(&system_context, &driver_context);
    result = test_start_session(&system_context, "standard_wash");
    TEST_ASSERT(result.ok);
    system_context.wait_condition.timeout_policy = WAIT_TIMEOUT_POLICY_ABORT_SESSION;
    system_context.wait_condition.max_retry_count = 3;
    result = test_fire_timeout(&system_context, 4000);
    TEST_ASSERT(result.ok);
    result = test_fire_timeout(&system_context, 4000);
    TEST_ASSERT(result.ok);
    result = test_fire_timeout(&system_context, 4000);
    TEST_ASSERT(result.ok);
    result = test_fire_timeout(&system_context, 4000);
    TEST_ASSERT(result.ok);
    TEST_ASSERT(system_context.wash_session.session_state == SESSION_STATE_ABORTED);
    return 0;
}

static int verify_idle_fault_clear_path(void)
{
    system_context_t system_context;
    simulated_driver_context_t driver_context;
    operation_result_t result;

    test_setup_system_context(&system_context, &driver_context);
    result = test_submit_fault_with_reason(&system_context, "E_STOP", "idle-fault");
    TEST_ASSERT(result.ok);
    TEST_ASSERT(system_context.global_fault_present);
    TEST_ASSERT(strcmp(system_context.last_result_code, "accepted") == 0);
    TEST_ASSERT(strcmp(system_context.last_reason_code, "global_fault_recorded") == 0);
    TEST_ASSERT(strcmp(system_context.last_transition_record.result_code, "accepted") == 0);
    TEST_ASSERT(strcmp(system_context.last_transition_record.reason_code, "global_fault_recorded") == 0);

    result = test_clear_fault(&system_context);
    TEST_ASSERT(result.ok);
    TEST_ASSERT(!system_context.global_fault_present);
    TEST_ASSERT(strcmp(system_context.last_result_code, "accepted") == 0);
    TEST_ASSERT(strcmp(system_context.last_reason_code, "global_fault_cleared") == 0);
    TEST_ASSERT(strcmp(system_context.last_transition_record.result_code, "accepted") == 0);
    TEST_ASSERT(strcmp(system_context.last_transition_record.reason_code, "global_fault_cleared") == 0);
    return 0;
}

int main(void)
{
    if (verify_stop_path() != 0) {
        return 1;
    }
    if (verify_fault_path() != 0) {
        return 1;
    }
    if (verify_timeout_path() != 0) {
        return 1;
    }
    if (verify_idle_fault_clear_path() != 0) {
        return 1;
    }
    return 0;
}
