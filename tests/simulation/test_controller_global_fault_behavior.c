#include "tests/test_support.h"
#include "src/application/coordinators/control_context_private.h"

int main(void)
{
    char fault_command[256];
    char response_line[512];
    char status_response[512];
    char long_fault_code[64];
    simulated_driver_context_t driver_context;
    operation_result_t result;
    size_t index;

    test_setup_system_context( &driver_context);
    result = test_load_runtime_program_from_fixture(
        "tests/fixtures/wash_step_control/program_v1_valid.json",
        0);
    TEST_ASSERT(result.ok);

    for (index = 0; index < sizeof(long_fault_code) - 1; ++index) {
        long_fault_code[index] = 'A';
    }
    long_fault_code[sizeof(long_fault_code) - 1] = '\0';
    snprintf(fault_command, sizeof(fault_command), "fault %s emergency_pressed", long_fault_code);

    result = test_process_command_and_flush( fault_command, response_line, sizeof(response_line));
    TEST_ASSERT(result.ok);
    TEST_ASSERT(strstr(response_line, "result=accepted accepted=true detail=global_fault_recorded") != 0);
    TEST_ASSERT(strcmp(control_context_private_runtime_mutable()->last_result_code, "accepted") == 0);
    TEST_ASSERT(strcmp(control_context_private_runtime_mutable()->last_reason_code, "global_fault_recorded") == 0);
    TEST_ASSERT(control_context_private_runtime_mutable()->global_fault_present == true);
    TEST_ASSERT(control_context_private_runtime_mutable()->global_fault_code[sizeof(control_context_private_runtime_mutable()->global_fault_code) - 1] == '\0');
    TEST_ASSERT(strlen(control_context_private_runtime_mutable()->global_fault_code) == sizeof(long_fault_code) - 1);
    TEST_ASSERT(strcmp(control_context_private_runtime_mutable()->global_fault_code, long_fault_code) == 0);

    result = test_process_command( "status", status_response, sizeof(status_response));
    TEST_ASSERT(result.ok);
    TEST_ASSERT(strstr(status_response, "result=status accepted=true") != 0);
    TEST_ASSERT(control_context_private_runtime_mutable()->global_fault_present == true);
    TEST_ASSERT(strcmp(control_context_private_runtime_mutable()->last_result_code, "accepted") == 0);
    TEST_ASSERT(strcmp(control_context_private_runtime_mutable()->last_reason_code, "global_fault_recorded") == 0);

    result = test_process_command_and_flush( "start wash_step_control_v1", response_line, sizeof(response_line));
    TEST_ASSERT(!result.ok);
    TEST_ASSERT(strstr(response_line, "accepted=false") != 0);
    TEST_ASSERT(strstr(response_line, "device_state_exception") != 0);

    result = test_process_command_and_flush( "fault clear", response_line, sizeof(response_line));
    TEST_ASSERT(result.ok);
    TEST_ASSERT(strstr(response_line, "result=accepted accepted=true detail=global_fault_cleared") != 0);
    TEST_ASSERT(strcmp(control_context_private_runtime_mutable()->last_result_code, "accepted") == 0);
    TEST_ASSERT(strcmp(control_context_private_runtime_mutable()->last_reason_code, "global_fault_cleared") == 0);
    TEST_ASSERT(control_context_private_runtime_mutable()->global_fault_present == false);
    TEST_ASSERT(control_context_private_runtime_mutable()->device_state == DEVICE_STATE_STOPPED);

    result = test_process_command_and_flush( "homing", response_line, sizeof(response_line));
    TEST_ASSERT(result.ok);
    TEST_ASSERT(strstr(response_line, "result=accepted accepted=true detail=homing_completed") != 0);
    TEST_ASSERT(control_context_private_runtime_mutable()->device_state == DEVICE_STATE_IDLE);

    result = test_process_command_and_flush( "start wash_step_control_v1", response_line, sizeof(response_line));
    TEST_ASSERT(result.ok);
    TEST_ASSERT(control_context_private_runtime_mutable()->wash_session.session_state == SESSION_STATE_RUNNING);
    TEST_ASSERT(control_context_private_runtime_mutable()->device_state == DEVICE_STATE_RUNNING);
    test_release_system_context();
    return 0;
}

