#include "tests/test_support.h"
#include "src/application/coordinators/control_context_private.h"

int main(void)
{
    operation_result_t result;
    simulated_driver_context_t driver_context;

    test_setup_system_context( &driver_context);
    result = test_load_runtime_program_from_fixture(
        "tests/fixtures/wash_step_control/program_v1_valid.json",
        0);
    TEST_ASSERT(result.ok);
    result = test_start_session_and_flush( "wash_step_control_v1");
    TEST_ASSERT(result.ok);

    result = test_tick( 100);
    TEST_ASSERT(result.ok);
    TEST_ASSERT(driver_context.chemical_command_count >= 1);

    driver_context.runtime_snapshot.position_snapshot.gantry_absolute_mm = 9500;
    driver_context.runtime_snapshot.position_snapshot.tail_reached = true;
    result = test_tick( 100);
    TEST_ASSERT(result.ok);
    driver_context.runtime_snapshot.actuator_feedback.roof_brush_home_reached = true;
    driver_context.runtime_snapshot.position_snapshot.tail_reached = false;
    result = test_tick( 100);
    TEST_ASSERT(result.ok);
    result = test_tick( 100);
    TEST_ASSERT(result.ok);
    TEST_ASSERT(driver_context.chemical_stop_command_count >= 1);
    TEST_ASSERT(strcmp(control_context_private_runtime_mutable()->wash_execution.segment_id, "side_segment") == 0);

    driver_context.runtime_snapshot.position_snapshot.gantry_absolute_mm = 2000;
    result = test_tick( 100);
    TEST_ASSERT(result.ok);
    TEST_ASSERT(driver_context.chemical_command_count == 1);

    driver_context.runtime_snapshot.position_snapshot.gantry_absolute_mm = 3500;
    result = test_tick( 100);
    TEST_ASSERT(result.ok);
    TEST_ASSERT(driver_context.chemical_command_count >= 2);
    test_release_system_context();
    return 0;
}

