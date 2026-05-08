#include "tests/test_support.h"

int main(void)
{
    char response_line[512];
    operation_result_t result;
    simulated_driver_context_t driver_context;
    system_context_t system_context;

    test_setup_system_context(&system_context, &driver_context);
    result = test_load_runtime_program_from_fixture(&system_context,
        "tests/fixtures/wash_step_control/program_v1_valid.json",
        0);
    TEST_ASSERT(result.ok);
    result = test_start_session(&system_context, "wash_step_control_v1");
    TEST_ASSERT(result.ok);

    driver_context.runtime_snapshot.position_snapshot.gantry_absolute_mm = 9500;
    driver_context.runtime_snapshot.position_snapshot.tail_reached = true;
    result = test_tick(&system_context, 100);
    TEST_ASSERT(result.ok);

    result = test_process_command(&system_context, "status", response_line, sizeof(response_line));
    TEST_ASSERT(result.ok);
    TEST_ASSERT(strstr(response_line, "lifecycle=exiting") != 0);
    TEST_ASSERT(strstr(response_line, "stage=roof_segment") != 0);
    return 0;
}
