#include "tests/test_support.h"
#include "src/application/coordinators/system_context_private.h"

int main(void)
{
    system_context_t system_context;
    simulated_driver_context_t driver_context;
    operation_result_t result;

    test_setup_system_context(&system_context, &driver_context);
    result = test_load_runtime_program_from_fixture(&system_context,
        "tests/fixtures/wash_step_control/program_v1_valid.json",
        0);
    TEST_ASSERT(result.ok);
    result = test_start_session_and_flush(&system_context, "wash_step_control_v1");
    TEST_ASSERT(result.ok);
    TEST_ASSERT(system_context.wash_session.session_state == SESSION_STATE_RUNNING);
    TEST_ASSERT(system_context.wash_execution.lifecycle_state == SEGMENT_LIFECYCLE_RUNNING);
    TEST_ASSERT(driver_context.motion_command_count == 1);
    TEST_ASSERT(driver_context.stop_all_command_count == 0);

    result = test_tick(&system_context, 100);
    TEST_ASSERT(result.ok);
    TEST_ASSERT(system_context.wash_execution.lifecycle_state == SEGMENT_LIFECYCLE_RUNNING);
    TEST_ASSERT(driver_context.motion_command_count == 1);
    TEST_ASSERT(driver_context.stop_all_command_count == 0);

    driver_context.runtime_snapshot.position_snapshot.gantry_absolute_mm = 9500;
    driver_context.runtime_snapshot.position_snapshot.tail_reached = true;
    result = test_tick(&system_context, 100);
    TEST_ASSERT(result.ok);
    TEST_ASSERT(system_context.wash_execution.lifecycle_state == SEGMENT_LIFECYCLE_EXITING);
    return 0;
}
