#include "application/use_cases/process_wash_trigger.h"
#include "application/use_cases/query_wash_session_status.h"
#include "tests/test_support.h"
#include "src/application/coordinators/control_context_private.h"

#define TEST_PROGRAM_FIXTURE_PATH "tests/fixtures/wash_step_control/program_v1_valid.json"
#define TEST_PROGRAM_ID "wash_step_control_v1"

typedef struct recovering_probe_context_t {
    actuator_port_t actuator_port;
    bool stop_all_observed_recovering;
    bool home_roof_observed_recovering;
} recovering_probe_context_t;

static int recovering_probe_stop_all(void *context, int timeout_ms)
{
    recovering_probe_context_t *recovering_probe_context;

    recovering_probe_context = context;
    recovering_probe_context->stop_all_observed_recovering =
        control_context_private_device_state() == DEVICE_STATE_RECOVERING;
    return recovering_probe_context->actuator_port.stop_all(
        recovering_probe_context->actuator_port.context,
        timeout_ms);
}

static int recovering_probe_home_roof_brush(void *context, int timeout_ms)
{
    recovering_probe_context_t *recovering_probe_context;

    recovering_probe_context = context;
    recovering_probe_context->home_roof_observed_recovering =
        control_context_private_device_state() == DEVICE_STATE_RECOVERING;
    return recovering_probe_context->actuator_port.home_roof_brush(
        recovering_probe_context->actuator_port.context,
        timeout_ms);
}

static void install_recovering_probe(recovering_probe_context_t *recovering_probe_context)
{
    actuator_port_t actuator_port;

    memset(recovering_probe_context, 0, sizeof(*recovering_probe_context));
    recovering_probe_context->actuator_port = *control_context_private_actuator_port();

    actuator_port = recovering_probe_context->actuator_port;
    actuator_port.context = recovering_probe_context;
    actuator_port.stop_all = recovering_probe_stop_all;
    actuator_port.home_roof_brush = recovering_probe_home_roof_brush;
    control_context_set_actuator_port( &actuator_port);
}

static void mark_session_ready_for_completion(void)
{
    int final_segment_index;
    wash_execution_t *wash_execution;
    const program_snapshot_t *program_snapshot;

    program_snapshot = control_context_private_program_snapshot();
    wash_execution = control_context_private_wash_execution_mutable();
    final_segment_index = program_snapshot->frozen_program.segment_count - 1;
    wash_execution->segment_index = final_segment_index;
    wash_execution->execution_state = EXECUTION_STATE_COMPLETED;
    wash_execution->lifecycle_state = SEGMENT_LIFECYCLE_COMPLETED;
    wash_execution->execution_result = EXECUTION_RESULT_SEGMENT_COMPLETED;
}

static int verify_completed_business_cycle_path(void)
{
    recovering_probe_context_t recovering_probe_context;
    simulated_driver_context_t driver_context;
    wash_session_status_view_t wash_session_status_view;
    operation_result_t result;

    test_setup_system_context( &driver_context);
    result = test_load_runtime_program_from_fixture( TEST_PROGRAM_FIXTURE_PATH, 0);
    TEST_ASSERT(result.ok);

    result = query_wash_session_status_execute( &wash_session_status_view);
    TEST_ASSERT(result.ok);
    TEST_ASSERT(!wash_session_status_view.has_active_session);
    TEST_ASSERT(wash_session_status_view.device_state == DEVICE_STATE_STOPPED);
    TEST_ASSERT(wash_session_status_view.session_state == SESSION_STATE_NONE);

    install_recovering_probe(&recovering_probe_context);
    result = test_homing_system_and_flush();
    TEST_ASSERT(result.ok);
    result = query_wash_session_status_execute( &wash_session_status_view);
    TEST_ASSERT(result.ok);
    TEST_ASSERT(recovering_probe_context.stop_all_observed_recovering);
    TEST_ASSERT(recovering_probe_context.home_roof_observed_recovering);
    TEST_ASSERT(!wash_session_status_view.has_active_session);
    TEST_ASSERT(wash_session_status_view.device_state == DEVICE_STATE_IDLE);
    TEST_ASSERT(wash_session_status_view.session_state == SESSION_STATE_NONE);
    TEST_ASSERT(strcmp(control_context_last_result_code(), "accepted") == 0);
    TEST_ASSERT(strcmp(control_context_last_reason_code(), "homing_completed") == 0);
    TEST_ASSERT(strcmp(wash_session_status_view.reason_code, "homing_completed") == 0);

    result = test_start_session_and_flush( TEST_PROGRAM_ID);
    TEST_ASSERT(result.ok);
    result = query_wash_session_status_execute( &wash_session_status_view);
    TEST_ASSERT(result.ok);
    TEST_ASSERT(wash_session_status_view.has_active_session);
    TEST_ASSERT(wash_session_status_view.device_state == DEVICE_STATE_RUNNING);
    TEST_ASSERT(wash_session_status_view.session_state == SESSION_STATE_RUNNING);
    TEST_ASSERT(wash_session_status_view.execution_state == EXECUTION_STATE_RUNNING);

    mark_session_ready_for_completion();
    result = process_wash_runtime_tick();
    TEST_ASSERT(result.ok);
    result = query_wash_session_status_execute( &wash_session_status_view);
    TEST_ASSERT(result.ok);
    TEST_ASSERT(!wash_session_status_view.has_active_session);
    TEST_ASSERT(wash_session_status_view.device_state == DEVICE_STATE_IDLE);
    TEST_ASSERT(wash_session_status_view.session_state == SESSION_STATE_COMPLETED);
    TEST_ASSERT(control_context_private_wash_session()->final_session_result == RESULT_CODE_SUCCESS);
    TEST_ASSERT(strcmp(control_context_last_result_code(), "completed") == 0);
    TEST_ASSERT(strcmp(control_context_last_reason_code(), "program_finished") == 0);
    TEST_ASSERT(strcmp(wash_session_status_view.reason_code, "program_finished") == 0);

    test_release_system_context();
    return 0;
}

static int verify_manual_stop_cycle_path(void)
{
    simulated_driver_context_t driver_context;
    wash_session_status_view_t wash_session_status_view;
    operation_result_t result;

    test_setup_system_context( &driver_context);
    result = test_load_runtime_program_from_fixture( TEST_PROGRAM_FIXTURE_PATH, 0);
    TEST_ASSERT(result.ok);
    result = test_homing_system_and_flush();
    TEST_ASSERT(result.ok);
    result = test_start_session_and_flush( TEST_PROGRAM_ID);
    TEST_ASSERT(result.ok);

    result = query_wash_session_status_execute( &wash_session_status_view);
    TEST_ASSERT(result.ok);
    TEST_ASSERT(wash_session_status_view.device_state == DEVICE_STATE_RUNNING);
    TEST_ASSERT(wash_session_status_view.session_state == SESSION_STATE_RUNNING);

    result = test_submit_stop( "manual-stop");
    TEST_ASSERT(result.ok);
    result = query_wash_session_status_execute( &wash_session_status_view);
    TEST_ASSERT(result.ok);
    TEST_ASSERT(!wash_session_status_view.has_active_session);
    TEST_ASSERT(wash_session_status_view.device_state == DEVICE_STATE_STOPPED);
    TEST_ASSERT(wash_session_status_view.session_state == SESSION_STATE_ABORTED);
    TEST_ASSERT(control_context_private_wash_session()->final_session_result == RESULT_CODE_MANUAL_ABORT);
    TEST_ASSERT(control_context_private_wash_execution()->execution_result == EXECUTION_RESULT_STOPPED);
    TEST_ASSERT(strcmp(control_context_last_result_code(), "aborted") == 0);
    TEST_ASSERT(strcmp(control_context_last_reason_code(), "manual-stop") == 0);
    TEST_ASSERT(strcmp(wash_session_status_view.reason_code, "manual-stop") == 0);

    test_release_system_context();
    return 0;
}

static int verify_fault_clear_recovery_cycle_path(void)
{
    recovering_probe_context_t recovering_probe_context;
    simulated_driver_context_t driver_context;
    wash_session_status_view_t wash_session_status_view;
    operation_result_t result;

    test_setup_system_context( &driver_context);
    result = test_load_runtime_program_from_fixture( TEST_PROGRAM_FIXTURE_PATH, 0);
    TEST_ASSERT(result.ok);
    result = test_homing_system_and_flush();
    TEST_ASSERT(result.ok);
    result = test_start_session_and_flush( TEST_PROGRAM_ID);
    TEST_ASSERT(result.ok);

    result = test_submit_fault( "fault-e-stop");
    TEST_ASSERT(result.ok);
    result = query_wash_session_status_execute( &wash_session_status_view);
    TEST_ASSERT(result.ok);
    TEST_ASSERT(!wash_session_status_view.has_active_session);
    TEST_ASSERT(wash_session_status_view.global_fault_present);
    TEST_ASSERT(wash_session_status_view.device_state == DEVICE_STATE_EXCEPTION);
    TEST_ASSERT(wash_session_status_view.session_state == SESSION_STATE_ABORTED);
    TEST_ASSERT(control_context_private_wash_session()->final_session_result == RESULT_CODE_SAFE_STOP);
    TEST_ASSERT(control_context_private_wash_execution()->execution_result == EXECUTION_RESULT_FAULTED);
    TEST_ASSERT(strcmp(control_context_last_result_code(), "aborted") == 0);
    TEST_ASSERT(strcmp(control_context_last_reason_code(), "fault-e-stop") == 0);
    TEST_ASSERT(strcmp(wash_session_status_view.reason_code, "fault-e-stop") == 0);

    result = test_clear_fault_and_flush();
    TEST_ASSERT(result.ok);
    result = query_wash_session_status_execute( &wash_session_status_view);
    TEST_ASSERT(result.ok);
    TEST_ASSERT(!wash_session_status_view.global_fault_present);
    TEST_ASSERT(wash_session_status_view.device_state == DEVICE_STATE_STOPPED);
    TEST_ASSERT(wash_session_status_view.session_state == SESSION_STATE_ABORTED);
    TEST_ASSERT(control_context_private_wash_session()->final_session_result == RESULT_CODE_SAFE_STOP);
    TEST_ASSERT(strcmp(control_context_last_result_code(), "accepted") == 0);
    TEST_ASSERT(strcmp(control_context_last_reason_code(), "global_fault_cleared") == 0);
    TEST_ASSERT(strcmp(wash_session_status_view.reason_code, "global_fault_cleared") == 0);

    install_recovering_probe(&recovering_probe_context);
    result = test_homing_system_and_flush();
    TEST_ASSERT(result.ok);
    result = query_wash_session_status_execute( &wash_session_status_view);
    TEST_ASSERT(result.ok);
    TEST_ASSERT(recovering_probe_context.stop_all_observed_recovering);
    TEST_ASSERT(recovering_probe_context.home_roof_observed_recovering);
    TEST_ASSERT(!wash_session_status_view.global_fault_present);
    TEST_ASSERT(wash_session_status_view.device_state == DEVICE_STATE_IDLE);
    TEST_ASSERT(wash_session_status_view.session_state == SESSION_STATE_ABORTED);
    TEST_ASSERT(control_context_private_wash_session()->final_session_result == RESULT_CODE_SAFE_STOP);
    TEST_ASSERT(strcmp(control_context_last_result_code(), "accepted") == 0);
    TEST_ASSERT(strcmp(control_context_last_reason_code(), "homing_completed") == 0);
    TEST_ASSERT(strcmp(wash_session_status_view.reason_code, "homing_completed") == 0);

    test_release_system_context();
    return 0;
}

int main(void)
{
    if (verify_completed_business_cycle_path() != 0) {
        return 1;
    }
    if (verify_manual_stop_cycle_path() != 0) {
        return 1;
    }
    if (verify_fault_clear_recovery_cycle_path() != 0) {
        return 1;
    }
    return 0;
}
