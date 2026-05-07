#include "tests/test_support.h"

static int failing_move_gantry(void *context, gantry_motion_mode_t motion_mode, int traverse_count, int timeout_ms)
{
    (void)context;
    (void)motion_mode;
    (void)traverse_count;
    (void)timeout_ms;
    return -1;
}

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
    TEST_ASSERT(strcmp(system_context.last_result_code, "accepted") == 0);
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
    TEST_ASSERT(strcmp(system_context.last_result_code, "accepted") == 0);
    TEST_ASSERT(strcmp(system_context.last_reason_code, "timeout_finish") == 0);
    TEST_ASSERT(system_context.last_transition_record.trigger_type == TRIGGER_TYPE_TIMEOUT);
    return 0;
}

static int verify_dispatch_failure_transition_source(void)
{
    system_context_t system_context;
    simulated_driver_context_t driver_context;
    operation_result_t result;

    test_setup_system_context(&system_context, &driver_context);
    system_context.actuator_port.move_gantry = failing_move_gantry;
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
