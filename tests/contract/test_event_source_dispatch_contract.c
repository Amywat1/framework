#include "tests/test_support.h"
#include "src/application/coordinators/control_context_private.h"

int main(void)
{
    scheduler_state_view_t app_state_view;
    simulated_driver_context_t driver_context;
    scheduler_t *scheduler;
    char response_line[512];
    operation_result_t result;

    test_setup_system_context(&driver_context);
    result = test_load_runtime_program_from_fixture(
        "tests/fixtures/wash_step_control/program_v1_valid.json",
        0);
    TEST_ASSERT(result.ok);

    scheduler = test_create_scheduler(100ul);
    TEST_ASSERT(scheduler != 0);

    TEST_ASSERT(test_scheduler_command(scheduler,
        "homing",
        response_line,
        sizeof(response_line)) == 0);
    TEST_ASSERT(strstr(response_line, "accepted=true") != 0);
    TEST_ASSERT(test_scheduler_notification(scheduler, 2u) == 0);
    TEST_ASSERT(control_context_private_runtime_mutable()->wash_session.session_state != SESSION_STATE_RUNNING);
    result = scheduler_read_view(scheduler, &app_state_view);
    TEST_ASSERT(result.ok);
    TEST_ASSERT(app_state_view.metrics.notification_event_count == 2ul);
    TEST_ASSERT(app_state_view.notification_source_state == SCHEDULER_EVENT_SOURCE_DEGRADED);

    TEST_ASSERT(test_scheduler_command(scheduler,
        "start wash_step_control_v1",
        response_line,
        sizeof(response_line)) == 0);
    TEST_ASSERT(strstr(response_line, "accepted=true") != 0);
    TEST_ASSERT(control_context_private_runtime_mutable()->wash_session.session_state == SESSION_STATE_RUNNING);

    test_release_system_context();
    test_setup_system_context(&driver_context);
    scheduler = test_create_scheduler(100ul);
    TEST_ASSERT(scheduler != 0);
    TEST_ASSERT(test_scheduler_exit(scheduler, false) == 0);
    result = scheduler_read_view(scheduler, &app_state_view);
    TEST_ASSERT(result.ok);
    TEST_ASSERT(app_state_view.metrics.exit_event_count == 1ul);
    TEST_ASSERT(app_state_view.runtime_state == SCHEDULER_RUNTIME_STATE_STOPPED);
    TEST_ASSERT(app_state_view.exit_source_state == SCHEDULER_EVENT_SOURCE_DEGRADED);

    /* 释放后，control_context_bound_scheduler 返回 0，test_scheduler_read_bound_view 应返回失败 */
    test_release_system_context();
    result = test_scheduler_read_bound_view(&app_state_view);
    TEST_ASSERT(!result.ok);
    TEST_ASSERT(result.error_code == ERROR_CODE_INVALID_STATE);
    return 0;
}
