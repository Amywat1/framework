#include "tests/test_support.h"

static void complete_roof_segment(simulated_driver_context_t *driver_context)
{
    driver_context->runtime_snapshot.position_snapshot.gantry_absolute_mm = 9500;
    driver_context->runtime_snapshot.position_snapshot.tail_reached = true;
}

static void complete_side_segment(simulated_driver_context_t *driver_context)
{
    driver_context->runtime_snapshot.position_snapshot.gantry_absolute_mm = 1000;
    driver_context->runtime_snapshot.position_snapshot.head_reached = true;
}

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

    complete_roof_segment(&driver_context);
    result = test_tick(&system_context, 100);
    TEST_ASSERT(result.ok);
    driver_context.runtime_snapshot.actuator_feedback.roof_brush_home_reached = true;
    driver_context.runtime_snapshot.position_snapshot.tail_reached = false;
    result = test_tick(&system_context, 100);
    TEST_ASSERT(result.ok);
    result = test_tick(&system_context, 100);
    TEST_ASSERT(result.ok);
    TEST_ASSERT(strcmp(system_context.wash_execution.segment_id, "side_segment") == 0);

    complete_side_segment(&driver_context);
    result = test_tick(&system_context, 100);
    TEST_ASSERT(result.ok);
    driver_context.runtime_snapshot.position_snapshot.head_reached = false;
    result = test_tick(&system_context, 100);
    TEST_ASSERT(result.ok);
    result = test_tick(&system_context, 100);
    TEST_ASSERT(result.ok);
    TEST_ASSERT(strcmp(system_context.wash_execution.segment_id, "ro_segment") == 0);
    return 0;
}
