#include "tests/test_support.h"

int main(void)
{
    char response_line[512];
    system_context_t system_context;
    simulated_driver_context_t driver_context;
    operation_result_t result;

    test_setup_system_context(&system_context, &driver_context);

    result = test_process_command(&system_context, "fault E_STOP emergency_pressed", response_line, sizeof(response_line));
    TEST_ASSERT(result.ok);
    TEST_ASSERT(strstr(response_line, "result=accepted accepted=true detail=global_fault_recorded") != 0);
    TEST_ASSERT(strcmp(system_context.last_result_code, "accepted") == 0);
    TEST_ASSERT(strcmp(system_context.last_transition_record.result_code, "accepted") == 0);
    TEST_ASSERT(strcmp(system_context.last_transition_record.reason_code, "global_fault_recorded") == 0);
    TEST_ASSERT(system_context.global_fault_present == true);

    result = test_process_command(&system_context, "start standard_wash", response_line, sizeof(response_line));
    TEST_ASSERT(!result.ok);
    TEST_ASSERT(strstr(response_line, "result=invalid_state") != 0);
    TEST_ASSERT(strstr(response_line, "global_fault_active") != 0);

    result = test_process_command(&system_context, "fault clear", response_line, sizeof(response_line));
    TEST_ASSERT(result.ok);
    TEST_ASSERT(strstr(response_line, "result=accepted accepted=true detail=global_fault_cleared") != 0);
    TEST_ASSERT(strcmp(system_context.last_result_code, "accepted") == 0);
    TEST_ASSERT(strcmp(system_context.last_transition_record.result_code, "accepted") == 0);
    TEST_ASSERT(strcmp(system_context.last_transition_record.reason_code, "global_fault_cleared") == 0);
    TEST_ASSERT(system_context.global_fault_present == false);

    result = test_process_command(&system_context, "start standard_wash", response_line, sizeof(response_line));
    TEST_ASSERT(result.ok);
    TEST_ASSERT(system_context.wash_session.session_state == SESSION_STATE_RUNNING);
    return 0;
}
