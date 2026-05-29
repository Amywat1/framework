#include "tests/test_support.h"

static const char *device_state_label(device_state_t device_state)
{
    switch (device_state) {
        case DEVICE_STATE_INIT:
            return "init";
        case DEVICE_STATE_RECOVERING:
            return "recovering";
        case DEVICE_STATE_IDLE:
            return "idle";
        case DEVICE_STATE_RUNNING:
            return "running";
        case DEVICE_STATE_EXCEPTION:
            return "exception";
        case DEVICE_STATE_STOPPED:
        default:
            return "stopped";
    }
}

static const char *session_state_label(session_state_t session_state)
{
    switch (session_state) {
        case SESSION_STATE_CREATED:
            return "created";
        case SESSION_STATE_RUNNING:
            return "running";
        case SESSION_STATE_COMPLETED:
            return "completed";
        case SESSION_STATE_ABORTED:
            return "aborted";
        case SESSION_STATE_NONE:
        default:
            return "none";
    }
}

static const char *execution_state_label(execution_state_t execution_state)
{
    switch (execution_state) {
        case EXECUTION_STATE_RUNNING:
            return "running";
        case EXECUTION_STATE_COMPLETED:
            return "completed";
        case EXECUTION_STATE_ABORTED:
            return "aborted";
        case EXECUTION_STATE_NONE:
        default:
            return "none";
    }
}

static int extract_field_value(const char *response_line, const char *field_name, char *value, size_t value_size)
{
    char pattern[32];
    const char *field_start;
    const char *field_end;
    size_t field_length;

    TEST_ASSERT(response_line != 0);
    TEST_ASSERT(field_name != 0);
    TEST_ASSERT(value != 0);
    TEST_ASSERT(value_size > 0u);

    snprintf(pattern, sizeof(pattern), "%s=", field_name);
    field_start = strstr(response_line, pattern);
    TEST_ASSERT(field_start != 0);

    field_start += strlen(pattern);
    field_end = strchr(field_start, ' ');
    field_length = field_end != 0 ? (size_t)(field_end - field_start) : strlen(field_start);
    TEST_ASSERT(field_length < value_size);

    memcpy(value, field_start, field_length);
    value[field_length] = '\0';
    return 0;
}

static int assert_status_single_line_matches_view(device_state_t expected_device_state)
{
    wash_session_status_view_t wash_session_status_view;
    char accepted_value[16];
    char device_value[32];
    char execution_value[32];
    char global_fault_value[16];
    char reason_value[64];
    char response_line[512];
    char result_value[32];
    char session_value[32];
    char state_value[32];
    operation_result_t result;

    result = query_wash_session_status_execute( &wash_session_status_view);
    TEST_ASSERT(result.ok);
    TEST_ASSERT(wash_session_status_view.device_state == expected_device_state);

    result = test_process_command( "status", response_line, sizeof(response_line));
    TEST_ASSERT(result.ok);
    TEST_ASSERT(strstr(response_line, "result=status accepted=true") != 0);
    TEST_ASSERT(strstr(response_line, "device=") != 0);
    TEST_ASSERT(strstr(response_line, "session=") != 0);
    TEST_ASSERT(strstr(response_line, "state=") != 0);
    TEST_ASSERT(strstr(response_line, "execution=") != 0);
    TEST_ASSERT(strstr(response_line, "global_fault=") != 0);
    TEST_ASSERT(strstr(response_line, "reason=") != 0);

    TEST_ASSERT(extract_field_value(response_line, "result", result_value, sizeof(result_value)) == 0);
    TEST_ASSERT(extract_field_value(response_line, "accepted", accepted_value, sizeof(accepted_value)) == 0);
    TEST_ASSERT(extract_field_value(response_line, "device", device_value, sizeof(device_value)) == 0);
    TEST_ASSERT(extract_field_value(response_line, "session", session_value, sizeof(session_value)) == 0);
    TEST_ASSERT(extract_field_value(response_line, "state", state_value, sizeof(state_value)) == 0);
    TEST_ASSERT(extract_field_value(response_line, "execution", execution_value, sizeof(execution_value)) == 0);
    TEST_ASSERT(extract_field_value(response_line, "global_fault", global_fault_value, sizeof(global_fault_value)) == 0);
    TEST_ASSERT(extract_field_value(response_line, "reason", reason_value, sizeof(reason_value)) == 0);

    TEST_ASSERT(strcmp(result_value, "status") == 0);
    TEST_ASSERT(strcmp(accepted_value, "true") == 0);
    TEST_ASSERT(strcmp(device_value, device_state_label(wash_session_status_view.device_state)) == 0);
    TEST_ASSERT(strcmp(session_value,
        wash_session_status_view.session_id[0] != '\0' ? wash_session_status_view.session_id : "none") == 0);
    TEST_ASSERT(strcmp(state_value, session_state_label(wash_session_status_view.session_state)) == 0);
    TEST_ASSERT(strcmp(execution_value, execution_state_label(wash_session_status_view.execution_state)) == 0);
    TEST_ASSERT(strcmp(global_fault_value, wash_session_status_view.global_fault_present ? "true" : "false") == 0);
    TEST_ASSERT(strcmp(reason_value,
        wash_session_status_view.reason_code[0] != '\0' ? wash_session_status_view.reason_code : "none") == 0);
    return 0;
}

static int verify_stopped_status_single_line_contract(void)
{
    simulated_driver_context_t driver_context;

    test_setup_system_context( &driver_context);
    TEST_ASSERT(assert_status_single_line_matches_view(DEVICE_STATE_STOPPED) == 0);
    test_release_system_context();
    return 0;
}

static int verify_running_status_single_line_contract(void)
{
    simulated_driver_context_t driver_context;
    operation_result_t result;

    test_setup_system_context( &driver_context);
    result = test_load_runtime_program_from_fixture(
        "tests/fixtures/wash_step_control/program_v1_valid.json",
        0);
    TEST_ASSERT(result.ok);
    result = test_start_session_and_flush( "wash_step_control_v1");
    TEST_ASSERT(result.ok);

    TEST_ASSERT(assert_status_single_line_matches_view(DEVICE_STATE_RUNNING) == 0);
    test_release_system_context();
    return 0;
}

static int verify_exception_status_single_line_contract(void)
{
    simulated_driver_context_t driver_context;
    char response_line[512];
    operation_result_t result;

    test_setup_system_context( &driver_context);
    result = test_process_command_and_flush(
        "fault E_STOP status-contract",
        response_line,
        sizeof(response_line));
    TEST_ASSERT(result.ok);
    TEST_ASSERT(strstr(response_line, "detail=global_fault_recorded") != 0);

    TEST_ASSERT(assert_status_single_line_matches_view(DEVICE_STATE_EXCEPTION) == 0);
    test_release_system_context();
    return 0;
}

int main(void)
{
    if (verify_stopped_status_single_line_contract() != 0) {
        return 1;
    }
    if (verify_running_status_single_line_contract() != 0) {
        return 1;
    }
    if (verify_exception_status_single_line_contract() != 0) {
        return 1;
    }
    return 0;
}
