#include "tests/test_support.h"
#include "src/application/coordinators/control_context_private.h"

int main(void)
{
    scheduler_state_view_t app_state_view;
    simulated_driver_context_t driver_context;
    scheduler_t *scheduler;
    char response_line[512];
    operation_result_t result;

    test_setup_system_context( &driver_context);
    result = test_load_runtime_program_from_fixture(
        "tests/fixtures/wash_step_control/program_v1_valid.json",
        0);
    TEST_ASSERT(result.ok);
    scheduler = test_create_scheduler( 100ul);
    TEST_ASSERT(scheduler != 0);

    TEST_ASSERT(test_scheduler_command(scheduler,
        "homing",
        response_line,
        sizeof(response_line)) == 0);
    TEST_ASSERT(test_scheduler_command(scheduler,
        "start wash_step_control_v1",
        response_line,
        sizeof(response_line)) == 0);
    TEST_ASSERT(control_context_private_wash_execution()->lifecycle_state == SEGMENT_LIFECYCLE_RUNNING);
    driver_context.runtime_snapshot.position_snapshot.gantry_absolute_mm = 9500;
    driver_context.runtime_snapshot.position_snapshot.tail_reached = true;
    TEST_ASSERT(test_scheduler_tick(scheduler, 1u) == 0);
    TEST_ASSERT(control_context_private_wash_execution()->lifecycle_state == SEGMENT_LIFECYCLE_EXITING);

    result = scheduler_read_view(scheduler, &app_state_view);
    TEST_ASSERT(result.ok);
    TEST_ASSERT(app_state_view.metrics.cycle_count == 1ul);

    test_release_system_context();
    return 0;
}

