#include "tests/test_support.h"
#include "src/application/coordinators/device_runtime_private.h"

int main(void)
{
    scheduler_state_view_t app_state_view;
    simulated_driver_context_t driver_context;
    wash_session_status_view_t wash_session_status_view;
    device_runtime_t system_context;
    scheduler_t *scheduler;
    char response_line[512];
    operation_result_t result;

    test_setup_system_context(&system_context, &driver_context);
    result = test_load_runtime_program_from_fixture(system_context,
        "tests/fixtures/wash_step_control/program_v1_valid.json",
        0);
    TEST_ASSERT(result.ok);

    scheduler = test_create_scheduler(system_context, 100ul);
    TEST_ASSERT(scheduler != 0);
    TEST_ASSERT(test_scheduler_command(scheduler,
        "homing",
        response_line,
        sizeof(response_line)) == 0);
    TEST_ASSERT(test_scheduler_command(scheduler,
        "start wash_step_control_v1",
        response_line,
        sizeof(response_line)) == 0);
    TEST_ASSERT(test_scheduler_notification(scheduler, 1u) == 0);
    TEST_ASSERT(test_scheduler_tick(scheduler, 2u) == 0);

    result = scheduler_read_view(scheduler, &app_state_view);
    TEST_ASSERT(result.ok);
    TEST_ASSERT(app_state_view.metrics.command_event_count == 2ul);
    TEST_ASSERT(app_state_view.metrics.notification_event_count == 1ul);
    TEST_ASSERT(app_state_view.metrics.cycle_count == 1ul);
    TEST_ASSERT(app_state_view.metrics.overrun_count == 1ul);

    result = query_wash_session_status_execute(system_context, &wash_session_status_view);
    TEST_ASSERT(result.ok);
    TEST_ASSERT(wash_session_status_view.scheduler_view_available);
    TEST_ASSERT(wash_session_status_view.scheduler_view.metrics.command_event_count == 2ul);
    TEST_ASSERT(wash_session_status_view.scheduler_view.metrics.notification_event_count == 1ul);

    test_release_system_context(system_context);
    return 0;
}

