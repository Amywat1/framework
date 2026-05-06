#include "tests/test_support.h"

static int verify_stop_transition_source(void)
{
    system_context_t system_context;
    simulated_driver_context_t driver_context;
    operation_result_t result;

    test_setup_system_context(&system_context, &driver_context);
    result = test_start_session(&system_context, "standard_wash");
    TEST_ASSERT(result.ok);
    TEST_ASSERT(strcmp(system_context.last_transition_record.current_state, "running") == 0);

    result = test_submit_stop(&system_context, "contract-stop");
    TEST_ASSERT(result.ok);
    TEST_ASSERT(system_context.wash_session.session_state == SESSION_STATE_ABORTED);
    TEST_ASSERT(strcmp(system_context.last_transition_record.reason_code, "contract-stop") == 0);
    TEST_ASSERT(system_context.last_transition_record.trigger_type == TRIGGER_TYPE_STOP);
    return 0;
}

static int verify_timeout_transition_source(void)
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
    TEST_ASSERT(system_context.last_transition_record.trigger_type == TRIGGER_TYPE_TIMEOUT);
    return 0;
}

static int verify_dispatch_failure_transition_source(void)
{
    system_context_t system_context;
    simulated_driver_context_t driver_context;
    operation_result_t result;

    test_setup_system_context(&system_context, &driver_context);
    driver_context.sensor_snapshot.resource_ok = false;
    result = test_start_session(&system_context, "standard_wash");
    TEST_ASSERT(result.ok);
    TEST_ASSERT(system_context.wash_session.session_state == SESSION_STATE_ABORTED);
    TEST_ASSERT(strcmp(system_context.wash_session.abort_reason, "dispatch_failed") == 0);
    TEST_ASSERT(system_context.last_transition_record.trigger_type == TRIGGER_TYPE_START);
    return 0;
}

int main(void)
{
    if (verify_stop_transition_source() != 0) {
        return 1;
    }
    if (verify_timeout_transition_source() != 0) {
        return 1;
    }
    if (verify_dispatch_failure_transition_source() != 0) {
        return 1;
    }
    return 0;
}
