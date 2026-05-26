#include "tests/test_support.h"
#include "src/application/coordinators/device_runtime_private.h"

#include "application/use_cases/query_wash_session_status.h"

static int verify_stop_path(void)
{
    device_runtime_t system_context;
    simulated_driver_context_t driver_context;
    operation_result_t result;

    test_setup_system_context(&system_context, &driver_context);
    result = test_start_session_and_flush(system_context, "standard_wash");
    TEST_ASSERT(result.ok);
    result = test_submit_stop(system_context, "integration-stop");
    TEST_ASSERT(result.ok);
    TEST_ASSERT(device_runtime_private_runtime(system_context)->wash_session.session_state == SESSION_STATE_ABORTED);
    TEST_ASSERT(device_runtime_private_runtime(system_context)->wash_session.final_session_result == RESULT_CODE_MANUAL_ABORT);
    TEST_ASSERT(device_runtime_private_runtime(system_context)->wash_execution.execution_result == EXECUTION_RESULT_STOPPED);
    TEST_ASSERT(strcmp(device_runtime_private_runtime(system_context)->last_result_code, "aborted") == 0);
    TEST_ASSERT(strcmp(device_runtime_private_runtime(system_context)->last_reason_code, "integration-stop") == 0);
    TEST_ASSERT(device_runtime_private_runtime(system_context)->device_state == DEVICE_STATE_STOPPED);
    return 0;
}

static int verify_fault_path(void)
{
    device_runtime_t system_context;
    simulated_driver_context_t driver_context;
    operation_result_t result;

    test_setup_system_context(&system_context, &driver_context);
    result = test_start_session_and_flush(system_context, "standard_wash");
    TEST_ASSERT(result.ok);
    result = test_submit_fault(system_context, "fault-e-stop");
    TEST_ASSERT(result.ok);
    TEST_ASSERT(device_runtime_private_runtime(system_context)->wash_session.session_state == SESSION_STATE_ABORTED);
    TEST_ASSERT(device_runtime_private_runtime(system_context)->wash_session.final_session_result == RESULT_CODE_SAFE_STOP);
    TEST_ASSERT(device_runtime_private_runtime(system_context)->wash_execution.execution_result == EXECUTION_RESULT_FAULTED);
    TEST_ASSERT(strcmp(device_runtime_private_runtime(system_context)->last_result_code, "aborted") == 0);
    TEST_ASSERT(strcmp(device_runtime_private_runtime(system_context)->last_reason_code, "fault-e-stop") == 0);
    TEST_ASSERT(device_runtime_private_runtime(system_context)->device_state == DEVICE_STATE_EXCEPTION);
    return 0;
}

static int verify_timeout_path(void)
{
    device_runtime_t system_context;
    simulated_driver_context_t driver_context;
    operation_result_t result;

    test_setup_system_context(&system_context, &driver_context);
    result = test_start_session_and_flush(system_context, "standard_wash");
    TEST_ASSERT(result.ok);
    device_runtime_private_runtime(system_context)->wait_condition.timeout_policy = WAIT_TIMEOUT_POLICY_SEGMENT;
    result = test_fire_timeout(system_context, 4000);
    TEST_ASSERT(result.ok);
    result = test_fire_timeout(system_context, 4000);
    TEST_ASSERT(result.ok);
    result = test_fire_timeout(system_context, 4000);
    TEST_ASSERT(result.ok);
    result = test_fire_timeout(system_context, 4000);
    TEST_ASSERT(result.ok);
    TEST_ASSERT(device_runtime_private_runtime(system_context)->wash_session.session_state == SESSION_STATE_ABORTED);
    TEST_ASSERT(device_runtime_private_runtime(system_context)->wash_session.final_session_result == RESULT_CODE_SEGMENT_TIMEOUT);
    TEST_ASSERT(device_runtime_private_runtime(system_context)->wash_execution.execution_result == EXECUTION_RESULT_SEGMENT_TIMEOUT);
    TEST_ASSERT(strcmp(device_runtime_private_runtime(system_context)->last_result_code, "aborted") == 0);
    TEST_ASSERT(strcmp(device_runtime_private_runtime(system_context)->last_reason_code, "segment_timeout") == 0);
    TEST_ASSERT(device_runtime_private_runtime(system_context)->device_state == DEVICE_STATE_EXCEPTION);
    return 0;
}

static int verify_idle_fault_clear_path(void)
{
    device_runtime_t system_context;
    simulated_driver_context_t driver_context;
    operation_result_t result;

    test_setup_system_context(&system_context, &driver_context);
    result = test_submit_fault_with_reason(system_context, "E_STOP", "idle-fault");
    TEST_ASSERT(result.ok);
    TEST_ASSERT(device_runtime_private_runtime(system_context)->global_fault_present);
    TEST_ASSERT(device_runtime_private_runtime(system_context)->device_state == DEVICE_STATE_EXCEPTION);
    TEST_ASSERT(strcmp(device_runtime_private_runtime(system_context)->last_result_code, "accepted") == 0);
    TEST_ASSERT(strcmp(device_runtime_private_runtime(system_context)->last_reason_code, "global_fault_recorded") == 0);
    TEST_ASSERT(strcmp(device_runtime_private_runtime(system_context)->last_transition_record.result_code, "accepted") == 0);
    TEST_ASSERT(strcmp(device_runtime_private_runtime(system_context)->last_transition_record.reason_code, "global_fault_recorded") == 0);

    result = test_clear_fault_and_flush(system_context);
    TEST_ASSERT(result.ok);
    TEST_ASSERT(!device_runtime_private_runtime(system_context)->global_fault_present);
    TEST_ASSERT(device_runtime_private_runtime(system_context)->device_state == DEVICE_STATE_STOPPED);
    TEST_ASSERT(strcmp(device_runtime_private_runtime(system_context)->last_result_code, "accepted") == 0);
    TEST_ASSERT(strcmp(device_runtime_private_runtime(system_context)->last_reason_code, "global_fault_cleared") == 0);
    TEST_ASSERT(strcmp(device_runtime_private_runtime(system_context)->last_transition_record.result_code, "accepted") == 0);
    TEST_ASSERT(strcmp(device_runtime_private_runtime(system_context)->last_transition_record.reason_code, "global_fault_cleared") == 0);

    result = device_runtime_reset(system_context);
    TEST_ASSERT(result.ok);
    TEST_ASSERT(!device_runtime_private_runtime(system_context)->global_fault_present);
    TEST_ASSERT(device_runtime_private_runtime(system_context)->wash_session.session_state == SESSION_STATE_NONE);
    TEST_ASSERT(device_runtime_private_runtime(system_context)->device_state == DEVICE_STATE_STOPPED);

    test_release_system_context(system_context);
    result = query_wash_session_status_execute(system_context, &(wash_session_status_view_t){0});
    TEST_ASSERT(!result.ok);
    TEST_ASSERT(result.error_code == ERROR_CODE_INVALID_STATE);
    return 0;
}

static int verify_direct_trigger_rejections_follow_command_matrix(void)
{
    device_runtime_t system_context;
    simulated_driver_context_t driver_context;
    operation_result_t result;

    test_setup_system_context(&system_context, &driver_context);

    result = test_submit_stop(system_context, "direct-stop");
    TEST_ASSERT(!result.ok);
    TEST_ASSERT(strcmp(device_runtime_private_runtime(system_context)->last_result_code, "rejected") == 0);
    TEST_ASSERT(strcmp(device_runtime_private_runtime(system_context)->last_reason_code, "stop_requires_running") == 0);
    TEST_ASSERT(device_runtime_private_runtime(system_context)->device_state == DEVICE_STATE_STOPPED);

    result = test_submit_fault_clear(system_context);
    TEST_ASSERT(!result.ok);
    TEST_ASSERT(strcmp(device_runtime_private_runtime(system_context)->last_result_code, "rejected") == 0);
    TEST_ASSERT(strcmp(device_runtime_private_runtime(system_context)->last_reason_code, "fault_clear_requires_exception") == 0);
    TEST_ASSERT(device_runtime_private_runtime(system_context)->device_state == DEVICE_STATE_STOPPED);

    result = test_homing_system_and_flush(system_context);
    TEST_ASSERT(result.ok);
    TEST_ASSERT(device_runtime_private_runtime(system_context)->device_state == DEVICE_STATE_IDLE);

    result = test_submit_stop(system_context, "direct-stop");
    TEST_ASSERT(!result.ok);
    TEST_ASSERT(strcmp(device_runtime_private_runtime(system_context)->last_result_code, "rejected") == 0);
    TEST_ASSERT(strcmp(device_runtime_private_runtime(system_context)->last_reason_code, "stop_requires_running") == 0);
    TEST_ASSERT(device_runtime_private_runtime(system_context)->device_state == DEVICE_STATE_IDLE);

    result = test_submit_fault_clear(system_context);
    TEST_ASSERT(!result.ok);
    TEST_ASSERT(strcmp(device_runtime_private_runtime(system_context)->last_result_code, "rejected") == 0);
    TEST_ASSERT(strcmp(device_runtime_private_runtime(system_context)->last_reason_code, "fault_clear_requires_exception") == 0);
    TEST_ASSERT(device_runtime_private_runtime(system_context)->device_state == DEVICE_STATE_IDLE);

    result = test_submit_fault_with_reason(system_context, "E_STOP", "direct-fault");
    TEST_ASSERT(result.ok);
    TEST_ASSERT(device_runtime_private_runtime(system_context)->device_state == DEVICE_STATE_EXCEPTION);

    result = test_submit_fault_clear(system_context);
    TEST_ASSERT(result.ok);
    TEST_ASSERT(strcmp(device_runtime_private_runtime(system_context)->last_result_code, "accepted") == 0);
    TEST_ASSERT(strcmp(device_runtime_private_runtime(system_context)->last_reason_code, "global_fault_cleared") == 0);
    TEST_ASSERT(device_runtime_private_runtime(system_context)->device_state == DEVICE_STATE_STOPPED);
    TEST_ASSERT(!device_runtime_private_runtime(system_context)->global_fault_present);

    test_release_system_context(system_context);
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
    if (verify_direct_trigger_rejections_follow_command_matrix() != 0) {
        return 1;
    }
    return 0;
}

