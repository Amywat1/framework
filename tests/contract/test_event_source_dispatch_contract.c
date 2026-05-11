#include "tests/test_support.h"
#include "src/application/coordinators/system_context_private.h"

int main(void)
{
    controller_runtime_state_view_t controller_runtime_state_view;
    simulated_driver_context_t driver_context;
    system_context_t released_handle;
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

    TEST_ASSERT(test_scheduler_notification(controller_scheduler, 2u) == 0);
    TEST_ASSERT(system_context_private_runtime(system_context)->wash_session.session_state != SESSION_STATE_RUNNING);
    result = controller_scheduler_read_view(controller_scheduler, &controller_runtime_state_view);
    TEST_ASSERT(result.ok);
    TEST_ASSERT(controller_runtime_state_view.metrics.notification_event_count == 2ul);
    TEST_ASSERT(controller_runtime_state_view.notification_source_state == CONTROLLER_SCHEDULER_EVENT_SOURCE_DEGRADED);

    TEST_ASSERT(test_scheduler_command(controller_scheduler,
        "start wash_step_control_v1",
        response_line,
        sizeof(response_line)) == 0);
    TEST_ASSERT(strstr(response_line, "accepted=true") != 0);
    TEST_ASSERT(system_context_private_runtime(system_context)->wash_session.session_state == SESSION_STATE_RUNNING);

    controller_scheduler_linux_destroy(controller_scheduler);
    test_setup_system_context(&system_context, &driver_context);
    controller_scheduler = test_create_scheduler(system_context, 100ul);
    TEST_ASSERT(controller_scheduler != 0);
    TEST_ASSERT(test_scheduler_exit(controller_scheduler, false) == 0);
    result = controller_scheduler_read_view(controller_scheduler, &controller_runtime_state_view);
    TEST_ASSERT(result.ok);
    TEST_ASSERT(controller_runtime_state_view.metrics.exit_event_count == 1ul);
    TEST_ASSERT(controller_runtime_state_view.runtime_state == CONTROLLER_SCHEDULER_RUNTIME_STATE_STOPPED);
    TEST_ASSERT(controller_runtime_state_view.exit_source_state == CONTROLLER_SCHEDULER_EVENT_SOURCE_DEGRADED);

    controller_scheduler_linux_destroy(controller_scheduler);
    released_handle = system_context;
    result = system_context_release(system_context);
    TEST_ASSERT(result.ok);
    result = controller_scheduler_read_context_view(released_handle, &controller_runtime_state_view);
    TEST_ASSERT(!result.ok);
    TEST_ASSERT(result.error_code == ERROR_CODE_INVALID_STATE);
    return 0;
}

