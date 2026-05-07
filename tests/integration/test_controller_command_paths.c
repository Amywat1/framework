#include "tests/test_support.h"

static int verify_precheck_block(void)
{
    char response_line[512];
    system_context_t system_context;
    simulated_driver_context_t driver_context;
    operation_result_t result;

    test_setup_system_context(&system_context, &driver_context);
    driver_context.sensor_snapshot.resource_ok = false;

    result = test_process_command(&system_context, "start standard_wash", response_line, sizeof(response_line));
    TEST_ASSERT(!result.ok);
    TEST_ASSERT(strstr(response_line, "result=precheck_failed") != 0);
    TEST_ASSERT(system_context.wash_session.session_state == SESSION_STATE_NONE);
    return 0;
}

static int verify_idle_command_paths(void)
{
    char response_line[512];
    system_context_t system_context;
    simulated_driver_context_t driver_context;
    operation_result_t result;

    test_setup_system_context(&system_context, &driver_context);

    result = test_process_command(&system_context, "stop", response_line, sizeof(response_line));
    TEST_ASSERT(result.ok);
    TEST_ASSERT(strstr(response_line, "result=ignored accepted=false detail=idle_stop") != 0);

    result = test_process_command(&system_context, "feedback feedback:pre_soak", response_line, sizeof(response_line));
    TEST_ASSERT(result.ok);
    TEST_ASSERT(strstr(response_line, "detail=idle_feedback") != 0);
    return 0;
}

static int verify_running_command_paths(void)
{
    char response_line[512];
    system_context_t system_context;
    simulated_driver_context_t driver_context;
    operation_result_t result;

    test_setup_system_context(&system_context, &driver_context);

    result = test_process_command(&system_context, "start standard_wash", response_line, sizeof(response_line));
    TEST_ASSERT(result.ok);
    TEST_ASSERT(strstr(response_line, "result=accepted accepted=true") != 0);

    result = test_process_command(&system_context, "feedback feedback:pre_soak", response_line, sizeof(response_line));
    TEST_ASSERT(result.ok);
    TEST_ASSERT(strstr(response_line, "result=accepted accepted=true") != 0);
    TEST_ASSERT(strcmp(system_context.wash_session.progress_stage_id, "wash") == 0);

    result = test_process_command(&system_context, "fault E_STOP running_fault", response_line, sizeof(response_line));
    TEST_ASSERT(result.ok);
    TEST_ASSERT(strstr(response_line, "result=accepted accepted=true detail=E_STOP") != 0);
    TEST_ASSERT(system_context.wash_session.session_state == SESSION_STATE_ABORTED);
    TEST_ASSERT(strcmp(system_context.wash_session.abort_reason, "E_STOP") == 0);
    return 0;
}

static int verify_feedback_command_respects_timeout_priority(void)
{
    bool has_trigger;
    char response_line[512];
    system_context_t system_context;
    simulated_driver_context_t driver_context;
    wash_trigger_event_t wash_trigger_event;
    operation_result_t result;

    test_setup_system_context(&system_context, &driver_context);
    result = test_start_session(&system_context, "standard_wash");
    TEST_ASSERT(result.ok);

    system_context.wait_condition.max_retry_count = 0;
    main_loop_advance_time(&system_context,
        system_context.wait_condition.timeout_at_ms - system_context.current_time_ms);
    result = cli_command_adapter_prepare_line(&system_context,
        "feedback feedback:pre_soak",
        &wash_trigger_event,
        &has_trigger,
        response_line,
        sizeof(response_line));
    TEST_ASSERT(result.ok);
    TEST_ASSERT(has_trigger == true);
    TEST_ASSERT(main_loop_submit_trigger(&system_context, &wash_trigger_event).ok);

    result = main_loop_run(&system_context);
    TEST_ASSERT(result.ok);
    TEST_ASSERT(has_pending_trigger_count(&system_context, wash_trigger_event.trigger_id) == 1);
    TEST_ASSERT(system_context.wash_session.session_state == SESSION_STATE_RUNNING);
    TEST_ASSERT(strcmp(system_context.wash_session.progress_stage_id, "wash") == 0);

    result = main_loop_run(&system_context);
    TEST_ASSERT(result.ok);
    TEST_ASSERT(has_pending_trigger_count(&system_context, wash_trigger_event.trigger_id) == 0);
    result = cli_command_adapter_finalize_trigger_response(&system_context, result, response_line, sizeof(response_line));
    TEST_ASSERT(result.ok);
    TEST_ASSERT(strstr(response_line, "result=ignored accepted=false detail=unmatched_feedback") != 0);
    return 0;
}

static int verify_start_command_rejects_running_session_before_timeout_resolution(void)
{
    bool has_trigger;
    char response_line[512];
    system_context_t system_context;
    simulated_driver_context_t driver_context;
    wash_trigger_event_t wash_trigger_event;
    operation_result_t result;

    test_setup_system_context(&system_context, &driver_context);
    result = test_start_session(&system_context, "standard_wash");
    TEST_ASSERT(result.ok);

    main_loop_advance_time(&system_context,
        system_context.wait_condition.timeout_at_ms - system_context.current_time_ms);
    result = cli_command_adapter_prepare_line(&system_context,
        "start standard_wash",
        &wash_trigger_event,
        &has_trigger,
        response_line,
        sizeof(response_line));
    TEST_ASSERT(!result.ok);
    TEST_ASSERT(has_trigger == false);
    TEST_ASSERT(strstr(response_line, "result=invalid_state accepted=false detail=running_session_exists") != 0);
    TEST_ASSERT(system_context.wash_session.session_state == SESSION_STATE_RUNNING);
    TEST_ASSERT(system_context.pending_trigger_count == 0);
    return 0;
}

int main(void)
{
    if (verify_precheck_block() != 0) {
        return 1;
    }
    if (verify_idle_command_paths() != 0) {
        return 1;
    }
    if (verify_running_command_paths() != 0) {
        return 1;
    }
    if (verify_feedback_command_respects_timeout_priority() != 0) {
        return 1;
    }
    if (verify_start_command_rejects_running_session_before_timeout_resolution() != 0) {
        return 1;
    }
    return 0;
}
