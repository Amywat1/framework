#include "tests/test_support.h"
#include "src/application/coordinators/system_context_private.h"

#include "application/use_cases/query_wash_session_status.h"

int main(void)
{
    controller_scheduler_t *controller_scheduler;
    system_context_t system_context;
    simulated_driver_context_t driver_context;
    wash_session_status_view_t wash_session_status_view;
    char response_line[512];
    char last_result_code_before[32];
    char last_reason_code_before[64];
    operation_result_t result;

    test_setup_system_context(&system_context, &driver_context);
    result = test_load_runtime_program_from_fixture(system_context,
        "tests/fixtures/wash_step_control/program_v1_valid.json",
        0);
    TEST_ASSERT(result.ok);
    controller_scheduler = test_create_scheduler(system_context, 100ul);
    TEST_ASSERT(controller_scheduler != 0);
    TEST_ASSERT(test_scheduler_command(controller_scheduler,
        "homing",
        response_line,
        sizeof(response_line)) == 0);
    TEST_ASSERT(test_scheduler_command(controller_scheduler,
        "start wash_step_control_v1",
        response_line,
        sizeof(response_line)) == 0);

    result = query_wash_session_status_execute(system_context, &wash_session_status_view);
    TEST_ASSERT(result.ok);
    TEST_ASSERT(wash_session_status_view.has_active_session);
    TEST_ASSERT(wash_session_status_view.device_state == DEVICE_STATE_RUNNING);
    TEST_ASSERT(wash_session_status_view.session_state == SESSION_STATE_RUNNING);
    TEST_ASSERT(wash_session_status_view.execution_state == EXECUTION_STATE_RUNNING);
    TEST_ASSERT(strcmp(wash_session_status_view.stage_id, "roof_segment") == 0);
    TEST_ASSERT(wash_session_status_view.scheduler_view_available);
    TEST_ASSERT(wash_session_status_view.scheduler_view.metrics.command_event_count == 2ul);
    strncpy(last_result_code_before, system_context_private_runtime(system_context)->last_result_code, sizeof(last_result_code_before) - 1);
    strncpy(last_reason_code_before, system_context_private_runtime(system_context)->last_reason_code, sizeof(last_reason_code_before) - 1);
    last_result_code_before[sizeof(last_result_code_before) - 1] = '\0';
    last_reason_code_before[sizeof(last_reason_code_before) - 1] = '\0';

    result = query_wash_session_status_execute(system_context, &wash_session_status_view);
    TEST_ASSERT(result.ok);
    TEST_ASSERT(strcmp(system_context_private_runtime(system_context)->last_result_code, last_result_code_before) == 0);
    TEST_ASSERT(strcmp(system_context_private_runtime(system_context)->last_reason_code, last_reason_code_before) == 0);
    TEST_ASSERT(system_context_private_runtime(system_context)->pending_trigger_count == 0u);

    result = test_submit_stop(system_context, "status-stop");
    TEST_ASSERT(result.ok);
    result = query_wash_session_status_execute(system_context, &wash_session_status_view);
    TEST_ASSERT(result.ok);
    TEST_ASSERT(!wash_session_status_view.has_active_session);
    TEST_ASSERT(wash_session_status_view.device_state == DEVICE_STATE_STOPPED);
    TEST_ASSERT(strcmp(wash_session_status_view.reason_code, "status-stop") == 0);

    result = system_context_reset(system_context);
    TEST_ASSERT(result.ok);
    result = query_wash_session_status_execute(system_context, &wash_session_status_view);
    TEST_ASSERT(result.ok);
    TEST_ASSERT(!wash_session_status_view.has_active_session);
    TEST_ASSERT(wash_session_status_view.device_state == DEVICE_STATE_STOPPED);
    TEST_ASSERT(wash_session_status_view.session_state == SESSION_STATE_NONE);
    TEST_ASSERT(wash_session_status_view.scheduler_view_available);
    TEST_ASSERT(wash_session_status_view.reason_code[0] == '\0');

    test_release_system_context(system_context);
    result = query_wash_session_status_execute(system_context, &wash_session_status_view);
    TEST_ASSERT(!result.ok);
    TEST_ASSERT(result.error_code == ERROR_CODE_INVALID_STATE);

    test_setup_system_context(&system_context, &driver_context);
    result = test_submit_fault_with_reason(system_context, "E_STOP", "idle-fault");
    TEST_ASSERT(result.ok);
    result = query_wash_session_status_execute(system_context, &wash_session_status_view);
    TEST_ASSERT(result.ok);
    TEST_ASSERT(!wash_session_status_view.has_active_session);
    TEST_ASSERT(wash_session_status_view.device_state == DEVICE_STATE_EXCEPTION);
    TEST_ASSERT(wash_session_status_view.global_fault_present);
    TEST_ASSERT(strcmp(wash_session_status_view.reason_code, "global_fault_recorded") == 0);
    TEST_ASSERT(strcmp(wash_session_status_view.global_fault_reason, "idle-fault") == 0);
    TEST_ASSERT(wash_session_status_view.scheduler_view_available);

    test_release_system_context(system_context);
    return 0;
}
