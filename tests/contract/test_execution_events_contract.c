#include "tests/test_support.h"
#include "src/application/coordinators/device_runtime_private.h"

static int failing_start_motion(void *context, const segment_motion_plan_t *segment_motion_plan, int timeout_ms)
{
    (void)context;
    (void)segment_motion_plan;
    (void)timeout_ms;
    return -1;
}

static int verify_stop_transition_source(void)
{
    device_runtime_t system_context;
    simulated_driver_context_t driver_context;
    operation_result_t result;

    test_setup_system_context(&system_context, &driver_context);
    result = test_start_session_and_flush(system_context, "standard_wash");
    TEST_ASSERT(result.ok);
    TEST_ASSERT(strcmp(device_runtime_private_runtime(system_context)->last_transition_record.current_state, "running") == 0);

    result = test_submit_stop(system_context, "contract-stop");
    TEST_ASSERT(result.ok);
    TEST_ASSERT(device_runtime_private_runtime(system_context)->wash_session.session_state == SESSION_STATE_ABORTED);
    TEST_ASSERT(strcmp(device_runtime_private_runtime(system_context)->last_result_code, "accepted") == 0);
    TEST_ASSERT(strcmp(device_runtime_private_runtime(system_context)->last_transition_record.reason_code, "contract-stop") == 0);
    TEST_ASSERT(device_runtime_private_runtime(system_context)->last_transition_record.trigger_type == TRIGGER_TYPE_STOP);
    return 0;
}

static int verify_timeout_transition_source(void)
{
    device_runtime_t system_context;
    simulated_driver_context_t driver_context;
    operation_result_t result;

    test_setup_system_context(&system_context, &driver_context);
    result = test_start_session_and_flush(system_context, "standard_wash");
    TEST_ASSERT(result.ok);
    device_runtime_private_runtime(system_context)->wait_condition.timeout_policy = WAIT_TIMEOUT_POLICY_SEGMENT;
    result = test_fire_timeout(system_context, 1000);
    TEST_ASSERT(result.ok);
    TEST_ASSERT(device_runtime_private_runtime(system_context)->wash_session.session_state == SESSION_STATE_ABORTED);
    TEST_ASSERT(strcmp(device_runtime_private_runtime(system_context)->last_result_code, "aborted") == 0);
    TEST_ASSERT(strcmp(device_runtime_private_runtime(system_context)->last_reason_code, "segment_timeout") == 0);
    TEST_ASSERT(device_runtime_private_runtime(system_context)->last_transition_record.trigger_type == TRIGGER_TYPE_TIMEOUT);
    return 0;
}

static int verify_dispatch_failure_transition_source(void)
{
    device_runtime_t system_context;
    simulated_driver_context_t driver_context;
    operation_result_t result;

    test_setup_system_context(&system_context, &driver_context);
    device_runtime_private_runtime(system_context)->actuator_port.start_motion = failing_start_motion;
    result = test_start_session_and_flush(system_context, "standard_wash");
    TEST_ASSERT(!result.ok);
    TEST_ASSERT(device_runtime_private_runtime(system_context)->wash_session.session_state == SESSION_STATE_ABORTED);
    TEST_ASSERT(strcmp(device_runtime_private_runtime(system_context)->wash_session.abort_reason, "dispatch_failed") == 0);
    TEST_ASSERT(strcmp(device_runtime_private_runtime(system_context)->last_reason_code, "dispatch_failed") == 0);
    TEST_ASSERT(device_runtime_private_runtime(system_context)->last_transition_record.trigger_type == TRIGGER_TYPE_START);
    return 0;
}

static int verify_reset_clears_transition_observation_and_release_blocks_it(void)
{
    device_runtime_t system_context;
    simulated_driver_context_t driver_context;
    wash_trigger_event_t wash_trigger_event;
    operation_result_t result;

    test_setup_system_context(&system_context, &driver_context);
    result = test_start_session_and_flush(system_context, "standard_wash");
    TEST_ASSERT(result.ok);
    result = test_submit_stop(system_context, "reset-stop");
    TEST_ASSERT(result.ok);
    TEST_ASSERT(device_runtime_private_runtime(system_context)->last_transition_record.current_state[0] != '\0');

    result = device_runtime_reset(system_context);
    TEST_ASSERT(result.ok);
    TEST_ASSERT(device_runtime_private_runtime(system_context)->last_result_code[0] == '\0');
    TEST_ASSERT(device_runtime_private_runtime(system_context)->last_reason_code[0] == '\0');
    TEST_ASSERT(device_runtime_private_runtime(system_context)->last_transition_record.current_state[0] == '\0');
    TEST_ASSERT(device_runtime_private_runtime(system_context)->last_transition_record.reason_code[0] == '\0');

    test_release_system_context(system_context);
    wash_trigger_event_init(&wash_trigger_event, TRIGGER_TYPE_STOP, 0, "released", "released", 0ul);
    result = process_wash_trigger_execute(system_context, &wash_trigger_event);
    TEST_ASSERT(!result.ok);
    TEST_ASSERT(result.error_code == ERROR_CODE_INVALID_STATE);
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
    if (verify_reset_clears_transition_observation_and_release_blocks_it() != 0) {
        return 1;
    }
    return 0;
}

