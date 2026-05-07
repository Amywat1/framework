#include "tests/test_support.h"

static int verify_idle_status(void)
{
    char response_line[512];
    system_context_t system_context;
    simulated_driver_context_t driver_context;
    operation_result_t result;

    test_setup_system_context(&system_context, &driver_context);
    result = test_process_command(&system_context, "status", response_line, sizeof(response_line));
    TEST_ASSERT(result.ok);
    TEST_ASSERT(strstr(response_line, "session=none") != 0);
    TEST_ASSERT(strstr(response_line, "global_fault=false") != 0);
    return 0;
}

static int verify_running_status(void)
{
    char response_line[512];
    system_context_t system_context;
    simulated_driver_context_t driver_context;
    operation_result_t result;

    test_setup_system_context(&system_context, &driver_context);
    result = test_process_command(&system_context, "start standard_wash", response_line, sizeof(response_line));
    TEST_ASSERT(result.ok);

    result = test_process_command(&system_context, "status", response_line, sizeof(response_line));
    TEST_ASSERT(result.ok);
    TEST_ASSERT(strstr(response_line, "state=running") != 0);
    TEST_ASSERT(strstr(response_line, "execution=running") != 0);
    TEST_ASSERT(strstr(response_line, "stage=pre_soak") != 0);
    TEST_ASSERT(strstr(response_line, "wait=feedback:pre_soak") != 0);
    return 0;
}

static int verify_global_fault_status(void)
{
    char response_line[512];
    system_context_t system_context;
    simulated_driver_context_t driver_context;
    operation_result_t result;

    test_setup_system_context(&system_context, &driver_context);
    result = test_process_command(&system_context, "fault E_STOP emergency_pressed", response_line, sizeof(response_line));
    TEST_ASSERT(result.ok);
    TEST_ASSERT(strcmp(system_context.last_result_code, "accepted") == 0);
    TEST_ASSERT(strcmp(system_context.last_reason_code, "global_fault_recorded") == 0);

    result = test_process_command(&system_context, "status", response_line, sizeof(response_line));
    TEST_ASSERT(result.ok);
    TEST_ASSERT(strstr(response_line, "session=none") != 0);
    TEST_ASSERT(strstr(response_line, "global_fault=true") != 0);
    TEST_ASSERT(strstr(response_line, "reason=global_fault_recorded") != 0);
    return 0;
}

int main(void)
{
    if (verify_idle_status() != 0) {
        return 1;
    }
    if (verify_running_status() != 0) {
        return 1;
    }
    if (verify_global_fault_status() != 0) {
        return 1;
    }
    return 0;
}
