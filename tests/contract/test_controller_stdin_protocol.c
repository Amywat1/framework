#include "tests/test_support.h"

static int verify_parse_failures(void)
{
    char response_line[512];
    system_context_t system_context;
    simulated_driver_context_t driver_context;
    operation_result_t result;

    test_setup_system_context(&system_context, &driver_context);

    result = test_process_command(&system_context, "start", response_line, sizeof(response_line));
    TEST_ASSERT(!result.ok);
    TEST_ASSERT(strstr(response_line, "result=parse_failed") != 0);
    TEST_ASSERT(strstr(response_line, "start_requires_program_id") != 0);

    result = test_process_command(&system_context, "feedback", response_line, sizeof(response_line));
    TEST_ASSERT(!result.ok);
    TEST_ASSERT(strstr(response_line, "feedback_requires_signal_code") != 0);

    result = test_process_command(&system_context, "fault E_STOP", response_line, sizeof(response_line));
    TEST_ASSERT(!result.ok);
    TEST_ASSERT(strstr(response_line, "fault_requires_reason") != 0);

    result = test_process_command(&system_context, "unknown command", response_line, sizeof(response_line));
    TEST_ASSERT(!result.ok);
    TEST_ASSERT(strstr(response_line, "result=unsupported") != 0);
    return 0;
}

static int verify_long_input_is_terminated(void)
{
    char response_line[512];
    char command_line[256];
    system_context_t system_context;
    simulated_driver_context_t driver_context;
    operation_result_t result;

    test_setup_system_context(&system_context, &driver_context);
    memset(command_line, 'x', sizeof(command_line) - 1);
    command_line[sizeof(command_line) - 1] = '\0';

    result = test_process_command(&system_context, command_line, response_line, sizeof(response_line));
    TEST_ASSERT(!result.ok);
    TEST_ASSERT(strstr(response_line, "result=unsupported") != 0);
    return 0;
}

static int verify_runtime_commands(void)
{
    char response_line[512];
    system_context_t system_context;
    simulated_driver_context_t driver_context;
    operation_result_t result;

    test_setup_system_context(&system_context, &driver_context);

    result = test_process_command(&system_context, "status", response_line, sizeof(response_line));
    TEST_ASSERT(result.ok);
    TEST_ASSERT(strstr(response_line, "result=status accepted=true") != 0);
    TEST_ASSERT(strstr(response_line, "session=none") != 0);
    TEST_ASSERT(strcmp(test_latest_result_code(&system_context), "none") == 0);

    result = test_process_command(&system_context, "start standard_wash", response_line, sizeof(response_line));
    TEST_ASSERT(result.ok);
    TEST_ASSERT(strstr(response_line, "result=accepted accepted=true") != 0);

    result = test_process_command(&system_context, "feedback feedback:pre_soak", response_line, sizeof(response_line));
    TEST_ASSERT(result.ok);
    TEST_ASSERT(strstr(response_line, "result=accepted accepted=true") != 0);

    result = test_process_command(&system_context, "stop", response_line, sizeof(response_line));
    TEST_ASSERT(result.ok);
    TEST_ASSERT(strstr(response_line, "result=accepted accepted=true") != 0);
    TEST_ASSERT(strstr(response_line, "manual-stop") != 0);
    return 0;
}

static int verify_fault_commands(void)
{
    char response_line[512];
    system_context_t system_context;
    simulated_driver_context_t driver_context;
    operation_result_t result;

    test_setup_system_context(&system_context, &driver_context);

    result = test_process_command(&system_context, "fault E_STOP emergency_pressed", response_line, sizeof(response_line));
    TEST_ASSERT(result.ok);
    TEST_ASSERT(strstr(response_line, "result=accepted accepted=true detail=global_fault_recorded") != 0);

    result = test_process_command(&system_context, "fault clear", response_line, sizeof(response_line));
    TEST_ASSERT(result.ok);
    TEST_ASSERT(strstr(response_line, "result=accepted accepted=true") != 0);
    TEST_ASSERT(strstr(response_line, "global_fault_cleared") != 0);
    result = test_process_command(&system_context, "status", response_line, sizeof(response_line));
    TEST_ASSERT(result.ok);
    TEST_ASSERT(strstr(response_line, "result=status accepted=true") != 0);
    TEST_ASSERT(strcmp(system_context.last_reason_code, "global_fault_cleared") == 0);
    return 0;
}

static int verify_queue_full_still_returns_protocol_line(void)
{
    char response_line[512];
    system_context_t system_context;
    simulated_driver_context_t driver_context;
    operation_result_t result;

    test_setup_system_context(&system_context, &driver_context);
    system_context.pending_trigger_count = MAX_PENDING_TRIGGER_COUNT;

    result = test_process_command(&system_context, "start standard_wash", response_line, sizeof(response_line));
    TEST_ASSERT(!result.ok);
    TEST_ASSERT(strstr(response_line, "result=resource_unavailable accepted=false detail=trigger_queue_full") != 0);
    TEST_ASSERT(strcmp(system_context.last_transition_record.result_code, "rejected") == 0);
    TEST_ASSERT(strcmp(system_context.last_transition_record.reason_code, "trigger_queue_full") == 0);
    return 0;
}

int main(void)
{
    if (verify_parse_failures() != 0) {
        return 1;
    }
    if (verify_long_input_is_terminated() != 0) {
        return 1;
    }
    if (verify_runtime_commands() != 0) {
        return 1;
    }
    if (verify_fault_commands() != 0) {
        return 1;
    }
    if (verify_queue_full_still_returns_protocol_line() != 0) {
        return 1;
    }
    return 0;
}
