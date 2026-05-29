#include "tests/test_support.h"
#include "src/application/coordinators/control_context_private.h"

int main(void)
{
    scheduler_state_view_t app_state_view;
    simulated_driver_context_t driver_context;
    scheduler_t *scheduler;
    char response_line[512];
    operation_result_t result;
    unsigned int pending_before;

    test_setup_system_context( &driver_context);
    result = test_load_runtime_program_from_fixture(
        "tests/fixtures/wash_step_control/program_v1_valid.json",
        0);
    TEST_ASSERT(result.ok);

    scheduler = test_create_scheduler( 100ul);
    TEST_ASSERT(scheduler != 0);

    pending_before = control_context_pending_trigger_count();
    TEST_ASSERT(test_scheduler_command(scheduler,
        "homing",
        response_line,
        sizeof(response_line)) == 0);
    TEST_ASSERT(strstr(response_line, "accepted=true") != 0);
    TEST_ASSERT(test_scheduler_command(scheduler,
        "start wash_step_control_v1",
        response_line,
        sizeof(response_line)) == 0);
    TEST_ASSERT(strstr(response_line, "accepted=true") != 0);
    TEST_ASSERT(control_context_pending_trigger_count() == pending_before);
    TEST_ASSERT(control_context_private_wash_session()->session_state == SESSION_STATE_RUNNING);

    TEST_ASSERT(test_scheduler_command(scheduler,
        "status",
        response_line,
        sizeof(response_line)) == 0);
    TEST_ASSERT(strstr(response_line, "result=status accepted=true") != 0);

    result = scheduler_read_view(scheduler, &app_state_view);
    TEST_ASSERT(result.ok);
    TEST_ASSERT(app_state_view.metrics.command_event_count == 3ul);

    test_release_system_context();
    return 0;
}

