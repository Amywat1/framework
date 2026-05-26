#include "tests/test_support.h"
#include "src/application/coordinators/device_runtime_private.h"

int main(void)
{
    operation_result_t result;
    simulated_driver_context_t driver_context;
    device_runtime_t system_context;

    test_setup_system_context(&system_context, &driver_context);
    result = test_load_runtime_program_from_fixture(system_context,
        "tests/fixtures/wash_step_control/program_v1_valid.json",
        0);
    TEST_ASSERT(result.ok);
    result = test_start_session_and_flush(system_context, "wash_step_control_v1");
    TEST_ASSERT(result.ok);

    result = test_tick(system_context, 100);
    TEST_ASSERT(result.ok);
    TEST_ASSERT(driver_context.chemical_command_count >= 1);

    driver_context.runtime_snapshot.position_snapshot.gantry_absolute_mm = 9500;
    driver_context.runtime_snapshot.position_snapshot.tail_reached = true;
    result = test_tick(system_context, 100);
    TEST_ASSERT(result.ok);
    driver_context.runtime_snapshot.actuator_feedback.roof_brush_home_reached = true;
    driver_context.runtime_snapshot.position_snapshot.tail_reached = false;
    result = test_tick(system_context, 100);
    TEST_ASSERT(result.ok);
    result = test_tick(system_context, 100);
    TEST_ASSERT(result.ok);
    TEST_ASSERT(driver_context.chemical_stop_command_count >= 1);
    TEST_ASSERT(strcmp(device_runtime_private_runtime(system_context)->wash_execution.segment_id, "side_segment") == 0);

    driver_context.runtime_snapshot.position_snapshot.gantry_absolute_mm = 2000;
    result = test_tick(system_context, 100);
    TEST_ASSERT(result.ok);
    TEST_ASSERT(driver_context.chemical_command_count == 1);

    driver_context.runtime_snapshot.position_snapshot.gantry_absolute_mm = 3500;
    result = test_tick(system_context, 100);
    TEST_ASSERT(result.ok);
    TEST_ASSERT(driver_context.chemical_command_count >= 2);
    test_release_system_context(system_context);
    return 0;
}

