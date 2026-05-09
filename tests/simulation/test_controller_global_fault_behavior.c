#include "tests/test_support.h"

int main(void)
{
    char fault_command[256];
    char response_line[512];
    char status_response[512];
    char long_fault_code[64];
    system_context_t system_context;
    simulated_driver_context_t driver_context;
    operation_result_t result;
    size_t index;

    test_setup_system_context(&system_context, &driver_context);
    result = test_load_runtime_program_from_fixture(&system_context,
        "tests/fixtures/wash_step_control/program_v1_valid.json",
        0);
    TEST_ASSERT(result.ok);

    for (index = 0; index < sizeof(long_fault_code) - 1; ++index) {
        long_fault_code[index] = 'A';
    }
    long_fault_code[sizeof(long_fault_code) - 1] = '\0';
    snprintf(fault_command, sizeof(fault_command), "fault %s emergency_pressed", long_fault_code);

    result = test_process_command(&system_context, fault_command, response_line, sizeof(response_line));
    TEST_ASSERT(result.ok);
    TEST_ASSERT(strstr(response_line, "result=accepted accepted=true detail=global_fault_recorded") != 0);
    TEST_ASSERT(strcmp(system_context.last_result_code, "accepted") == 0);
    TEST_ASSERT(strcmp(system_context.last_reason_code, "global_fault_recorded") == 0);
    TEST_ASSERT(system_context.global_fault_present == true);
    TEST_ASSERT(system_context.global_fault_code[sizeof(system_context.global_fault_code) - 1] == '\0');
    TEST_ASSERT(strlen(system_context.global_fault_code) == sizeof(long_fault_code) - 1);
    TEST_ASSERT(strcmp(system_context.global_fault_code, long_fault_code) == 0);

    result = test_process_command(&system_context, "status", status_response, sizeof(status_response));
    TEST_ASSERT(result.ok);
    TEST_ASSERT(strstr(status_response, "result=status accepted=true") != 0);
    TEST_ASSERT(system_context.global_fault_present == true);
    TEST_ASSERT(strcmp(system_context.last_result_code, "accepted") == 0);
    TEST_ASSERT(strcmp(system_context.last_reason_code, "global_fault_recorded") == 0);

    result = test_process_command(&system_context, "start wash_step_control_v1", response_line, sizeof(response_line));
    TEST_ASSERT(!result.ok);
    TEST_ASSERT(strstr(response_line, "result=invalid_state") != 0);
    TEST_ASSERT(strstr(response_line, "global_fault_active") != 0);

    result = test_process_command(&system_context, "fault clear", response_line, sizeof(response_line));
    TEST_ASSERT(result.ok);
    TEST_ASSERT(strstr(response_line, "result=accepted accepted=true detail=global_fault_cleared") != 0);
    TEST_ASSERT(strcmp(system_context.last_result_code, "accepted") == 0);
    TEST_ASSERT(strcmp(system_context.last_reason_code, "global_fault_cleared") == 0);
    TEST_ASSERT(system_context.global_fault_present == false);

    result = test_process_command(&system_context, "start wash_step_control_v1", response_line, sizeof(response_line));
    TEST_ASSERT(result.ok);
    TEST_ASSERT(system_context.wash_session.session_state == SESSION_STATE_RUNNING);
    return 0;
}
