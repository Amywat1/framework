#include "tests/test_support.h"
#include "src/application/coordinators/system_context_private.h"

int main(void)
{
    controller_runtime_state_view_t controller_runtime_state_view;
    simulated_driver_context_t driver_context;
    wash_session_status_view_t wash_session_status_view;
    system_context_t system_context;
    controller_scheduler_t *controller_scheduler;
    char response_line[512];
    operation_result_t result;

    test_setup_system_context(&system_context, &driver_context);
    result = test_load_runtime_program_from_fixture(system_context,
        "tests/fixtures/wash_step_control/program_v1_valid.json",
        0);
    TEST_ASSERT(result.ok);

    controller_scheduler = test_create_scheduler(system_context, 100ul);
    TEST_ASSERT(controller_scheduler != 0);
    TEST_ASSERT(test_scheduler_command(controller_scheduler,
        "start wash_step_control_v1",
        response_line,
        sizeof(response_line)) == 0);
    TEST_ASSERT(test_scheduler_notification(controller_scheduler, 1u) == 0);
    TEST_ASSERT(test_scheduler_tick(controller_scheduler, 2u) == 0);

    result = controller_scheduler_read_view(controller_scheduler, &controller_runtime_state_view);
    TEST_ASSERT(result.ok);
    TEST_ASSERT(controller_runtime_state_view.metrics.command_event_count == 1ul);
    TEST_ASSERT(controller_runtime_state_view.metrics.notification_event_count == 1ul);
    TEST_ASSERT(controller_runtime_state_view.metrics.cycle_count == 1ul);
    TEST_ASSERT(controller_runtime_state_view.metrics.overrun_count == 1ul);

    result = query_wash_session_status_execute(system_context, &wash_session_status_view);
    TEST_ASSERT(result.ok);
    TEST_ASSERT(wash_session_status_view.scheduler_view_available);
    TEST_ASSERT(wash_session_status_view.scheduler_view.metrics.command_event_count == 1ul);
    TEST_ASSERT(wash_session_status_view.scheduler_view.metrics.notification_event_count == 1ul);

    controller_scheduler_linux_destroy(controller_scheduler);
    test_release_system_context(system_context);
    return 0;
}

