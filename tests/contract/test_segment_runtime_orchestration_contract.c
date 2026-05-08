#include "tests/test_support.h"

int main(void)
{
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
    TEST_ASSERT(system_context.wash_session.session_state == SESSION_STATE_RUNNING);
    TEST_ASSERT(system_context.wash_execution.lifecycle_state == SEGMENT_LIFECYCLE_RUNNING);
    TEST_ASSERT(strcmp(system_context.wash_execution.segment_id, "roof_segment") == 0);

    driver_context.runtime_snapshot.position_snapshot.gantry_absolute_mm = 9500;
    driver_context.runtime_snapshot.position_snapshot.tail_reached = true;
    result = test_tick(&system_context, 100);
    TEST_ASSERT(result.ok);
    TEST_ASSERT(system_context.wash_execution.lifecycle_state == SEGMENT_LIFECYCLE_EXITING);
    TEST_ASSERT(driver_context.roof_stop_command_count == 1);
    TEST_ASSERT(driver_context.chemical_stop_command_count == 1);
    TEST_ASSERT(driver_context.roof_home_command_count == 1);

    driver_context.runtime_snapshot.actuator_feedback.roof_brush_home_reached = true;
    driver_context.runtime_snapshot.position_snapshot.tail_reached = false;
    result = test_tick(&system_context, 100);
    TEST_ASSERT(result.ok);
    TEST_ASSERT(strcmp(system_context.wash_execution.segment_id, "side_segment") == 0);
    TEST_ASSERT(system_context.wash_execution.lifecycle_state == SEGMENT_LIFECYCLE_ENTERING);
    return 0;
}
