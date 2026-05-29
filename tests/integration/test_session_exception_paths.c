#include "tests/test_support.h"
#include "src/application/coordinators/control_context_private.h"

#include "application/use_cases/query_wash_session_status.h"

static int verify_stop_path(void)
{
    simulated_driver_context_t driver_context;
    operation_result_t result;

    test_setup_system_context( &driver_context);
    result = test_start_session_and_flush( "standard_wash");
    TEST_ASSERT(result.ok);
    result = test_submit_stop( "integration-stop");
    TEST_ASSERT(result.ok);
    TEST_ASSERT(control_context_private_runtime_mutable()->wash_session.session_state == SESSION_STATE_ABORTED);
    TEST_ASSERT(control_context_private_runtime_mutable()->wash_session.final_session_result == RESULT_CODE_MANUAL_ABORT);
    TEST_ASSERT(control_context_private_runtime_mutable()->wash_execution.execution_result == EXECUTION_RESULT_STOPPED);
    TEST_ASSERT(strcmp(control_context_private_runtime_mutable()->last_result_code, "aborted") == 0);
    TEST_ASSERT(strcmp(control_context_private_runtime_mutable()->last_reason_code, "integration-stop") == 0);
    TEST_ASSERT(control_context_private_runtime_mutable()->device_state == DEVICE_STATE_STOPPED);
    return 0;
}

static int verify_fault_path(void)
{
    simulated_driver_context_t driver_context;
    operation_result_t result;

    test_setup_system_context( &driver_context);
    result = test_start_session_and_flush( "standard_wash");
    TEST_ASSERT(result.ok);
    result = test_submit_fault( "fault-e-stop");
    TEST_ASSERT(result.ok);
    TEST_ASSERT(control_context_private_runtime_mutable()->wash_session.session_state == SESSION_STATE_ABORTED);
    TEST_ASSERT(control_context_private_runtime_mutable()->wash_session.final_session_result == RESULT_CODE_SAFE_STOP);
    TEST_ASSERT(control_context_private_runtime_mutable()->wash_execution.execution_result == EXECUTION_RESULT_FAULTED);
    TEST_ASSERT(strcmp(control_context_private_runtime_mutable()->last_result_code, "aborted") == 0);
    TEST_ASSERT(strcmp(control_context_private_runtime_mutable()->last_reason_code, "fault-e-stop") == 0);
    TEST_ASSERT(control_context_private_runtime_mutable()->device_state == DEVICE_STATE_EXCEPTION);
    return 0;
}

static int verify_timeout_path(void)
{
    simulated_driver_context_t driver_context;
    operation_result_t result;

    test_setup_system_context( &driver_context);
    result = test_start_session_and_flush( "standard_wash");
    TEST_ASSERT(result.ok);
    control_context_private_runtime_mutable()->wait_condition.timeout_policy = WAIT_TIMEOUT_POLICY_SEGMENT;
    result = test_fire_timeout( 4000);
    TEST_ASSERT(result.ok);
    result = test_fire_timeout( 4000);
    TEST_ASSERT(result.ok);
    result = test_fire_timeout( 4000);
    TEST_ASSERT(result.ok);
    result = test_fire_timeout( 4000);
    TEST_ASSERT(result.ok);
    TEST_ASSERT(control_context_private_runtime_mutable()->wash_session.session_state == SESSION_STATE_ABORTED);
    TEST_ASSERT(control_context_private_runtime_mutable()->wash_session.final_session_result == RESULT_CODE_SEGMENT_TIMEOUT);
    TEST_ASSERT(control_context_private_runtime_mutable()->wash_execution.execution_result == EXECUTION_RESULT_SEGMENT_TIMEOUT);
    TEST_ASSERT(strcmp(control_context_private_runtime_mutable()->last_result_code, "aborted") == 0);
    TEST_ASSERT(strcmp(control_context_private_runtime_mutable()->last_reason_code, "segment_timeout") == 0);
    TEST_ASSERT(control_context_private_runtime_mutable()->device_state == DEVICE_STATE_EXCEPTION);
    return 0;
}

static int verify_idle_fault_clear_path(void)
{
    simulated_driver_context_t driver_context;
    operation_result_t result;

    test_setup_system_context( &driver_context);
    result = test_submit_fault_with_reason( "E_STOP", "idle-fault");
    TEST_ASSERT(result.ok);
    TEST_ASSERT(control_context_private_runtime_mutable()->global_fault_present);
    TEST_ASSERT(control_context_private_runtime_mutable()->device_state == DEVICE_STATE_EXCEPTION);
    TEST_ASSERT(strcmp(control_context_private_runtime_mutable()->last_result_code, "accepted") == 0);
    TEST_ASSERT(strcmp(control_context_private_runtime_mutable()->last_reason_code, "global_fault_recorded") == 0);
    TEST_ASSERT(strcmp(control_context_private_runtime_mutable()->last_transition_record.result_code, "accepted") == 0);
    TEST_ASSERT(strcmp(control_context_private_runtime_mutable()->last_transition_record.reason_code, "global_fault_recorded") == 0);

    result = test_clear_fault_and_flush();
    TEST_ASSERT(result.ok);
    TEST_ASSERT(!control_context_private_runtime_mutable()->global_fault_present);
    TEST_ASSERT(control_context_private_runtime_mutable()->device_state == DEVICE_STATE_STOPPED);
    TEST_ASSERT(strcmp(control_context_private_runtime_mutable()->last_result_code, "accepted") == 0);
    TEST_ASSERT(strcmp(control_context_private_runtime_mutable()->last_reason_code, "global_fault_cleared") == 0);
    TEST_ASSERT(strcmp(control_context_private_runtime_mutable()->last_transition_record.result_code, "accepted") == 0);
    TEST_ASSERT(strcmp(control_context_private_runtime_mutable()->last_transition_record.reason_code, "global_fault_cleared") == 0);

    result = control_context_reset();
    TEST_ASSERT(result.ok);
    TEST_ASSERT(!control_context_private_runtime_mutable()->global_fault_present);
    TEST_ASSERT(control_context_private_runtime_mutable()->wash_session.session_state == SESSION_STATE_NONE);
    TEST_ASSERT(control_context_private_runtime_mutable()->device_state == DEVICE_STATE_STOPPED);

    test_release_system_context();
    result = query_wash_session_status_execute( &(wash_session_status_view_t){0});
    TEST_ASSERT(!result.ok);
    TEST_ASSERT(result.error_code == ERROR_CODE_INVALID_STATE);
    return 0;
}

static int verify_direct_trigger_rejections_follow_command_matrix(void)
{
    simulated_driver_context_t driver_context;
    operation_result_t result;

    test_setup_system_context( &driver_context);

    result = test_submit_stop( "direct-stop");
    TEST_ASSERT(!result.ok);
    TEST_ASSERT(strcmp(control_context_private_runtime_mutable()->last_result_code, "rejected") == 0);
    TEST_ASSERT(strcmp(control_context_private_runtime_mutable()->last_reason_code, "stop_requires_running") == 0);
    TEST_ASSERT(control_context_private_runtime_mutable()->device_state == DEVICE_STATE_STOPPED);

    result = test_submit_fault_clear();
    TEST_ASSERT(!result.ok);
    TEST_ASSERT(strcmp(control_context_private_runtime_mutable()->last_result_code, "rejected") == 0);
    TEST_ASSERT(strcmp(control_context_private_runtime_mutable()->last_reason_code, "fault_clear_requires_exception") == 0);
    TEST_ASSERT(control_context_private_runtime_mutable()->device_state == DEVICE_STATE_STOPPED);

    result = test_homing_system_and_flush();
    TEST_ASSERT(result.ok);
    TEST_ASSERT(control_context_private_runtime_mutable()->device_state == DEVICE_STATE_IDLE);

    result = test_submit_stop( "direct-stop");
    TEST_ASSERT(!result.ok);
    TEST_ASSERT(strcmp(control_context_private_runtime_mutable()->last_result_code, "rejected") == 0);
    TEST_ASSERT(strcmp(control_context_private_runtime_mutable()->last_reason_code, "stop_requires_running") == 0);
    TEST_ASSERT(control_context_private_runtime_mutable()->device_state == DEVICE_STATE_IDLE);

    result = test_submit_fault_clear();
    TEST_ASSERT(!result.ok);
    TEST_ASSERT(strcmp(control_context_private_runtime_mutable()->last_result_code, "rejected") == 0);
    TEST_ASSERT(strcmp(control_context_private_runtime_mutable()->last_reason_code, "fault_clear_requires_exception") == 0);
    TEST_ASSERT(control_context_private_runtime_mutable()->device_state == DEVICE_STATE_IDLE);

    result = test_submit_fault_with_reason( "E_STOP", "direct-fault");
    TEST_ASSERT(result.ok);
    TEST_ASSERT(control_context_private_runtime_mutable()->device_state == DEVICE_STATE_EXCEPTION);

    result = test_submit_fault_clear();
    TEST_ASSERT(result.ok);
    TEST_ASSERT(strcmp(control_context_private_runtime_mutable()->last_result_code, "accepted") == 0);
    TEST_ASSERT(strcmp(control_context_private_runtime_mutable()->last_reason_code, "global_fault_cleared") == 0);
    TEST_ASSERT(control_context_private_runtime_mutable()->device_state == DEVICE_STATE_STOPPED);
    TEST_ASSERT(!control_context_private_runtime_mutable()->global_fault_present);

    test_release_system_context();
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

