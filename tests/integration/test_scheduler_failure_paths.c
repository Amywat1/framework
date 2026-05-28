#include "tests/test_support.h"
#include "src/application/coordinators/device_runtime_private.h"

static int verify_main_loop_failure_is_terminal(void)
{
    scheduler_state_view_t app_state_view;
    simulated_driver_context_t driver_context;
    scheduler_t *scheduler;
    operation_result_t result;

    test_setup_system_context( &driver_context);
    scheduler = test_create_scheduler( 100ul);
    TEST_ASSERT(scheduler != 0);

    scheduler_linux_test_set_failpoint(scheduler,
        SCHEDULER_LINUX_TEST_FAIL_CONTROL_TICK_RUN,
        true);
    result = scheduler_linux_test_inject_period(scheduler, 1u);
    TEST_ASSERT(!result.ok);
    result = scheduler_read_view(scheduler, &app_state_view);
    TEST_ASSERT(result.ok);
    TEST_ASSERT(app_state_view.runtime_state == SCHEDULER_RUNTIME_STATE_FAILED);
    TEST_ASSERT(strcmp(app_state_view.metrics.last_error_reason, "control_tick_run_failed") == 0);

    test_release_system_context();
    return 0;
}

static int verify_wakeup_failure_is_terminal(void)
{
    scheduler_state_view_t app_state_view;
    simulated_driver_context_t driver_context;
    scheduler_t *scheduler;
    operation_result_t result;

    test_setup_system_context( &driver_context);
    scheduler = test_create_scheduler( 100ul);
    TEST_ASSERT(scheduler != 0);

    scheduler_linux_test_set_failpoint(scheduler,
        SCHEDULER_LINUX_TEST_FAIL_WAKEUP_WRITE,
        true);
    result = scheduler_request_stop(scheduler);
    TEST_ASSERT(!result.ok);
    result = scheduler_read_view(scheduler, &app_state_view);
    TEST_ASSERT(result.ok);
    TEST_ASSERT(app_state_view.runtime_state == SCHEDULER_RUNTIME_STATE_FAILED);
    TEST_ASSERT(strcmp(app_state_view.metrics.last_error_reason, "wakeup_write_failed") == 0);

    test_release_system_context();
    return 0;
}

static int verify_command_path_does_not_swallow_runtime_failure(void)
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

    result = scheduler_linux_test_inject_command(scheduler,
        "homing",
        response_line,
        sizeof(response_line));
    TEST_ASSERT(result.ok);
    TEST_ASSERT(strcmp(device_runtime_private_runtime_mutable()->last_result_code, "accepted") == 0);

    scheduler_linux_test_set_failpoint(scheduler,
        SCHEDULER_LINUX_TEST_FAIL_CONTROL_TICK_RUN,
        true);
    result = scheduler_linux_test_inject_command(scheduler,
        "start wash_step_control_v1",
        response_line,
        sizeof(response_line));
    TEST_ASSERT(!result.ok);
    result = scheduler_read_view(scheduler, &app_state_view);
    TEST_ASSERT(result.ok);
    TEST_ASSERT(app_state_view.runtime_state == SCHEDULER_RUNTIME_STATE_FAILED);
    TEST_ASSERT(strcmp(app_state_view.metrics.last_error_reason, "control_tick_run_failed") == 0);

    test_release_system_context();
    return 0;
}

static int verify_released_context_is_rejected_by_scheduler_boundary(void)
{
    scheduler_state_view_t app_state_view;
    simulated_driver_context_t driver_context;
    operation_result_t result;

    test_setup_system_context( &driver_context);
    test_release_system_context();

    TEST_ASSERT(test_scheduler_create_unbound( 0, 0) == 0);
    result = test_scheduler_read_bound_view( &app_state_view);
    TEST_ASSERT(!result.ok);
    TEST_ASSERT(result.error_code == ERROR_CODE_INVALID_STATE);
    return 0;
}

int main(void)
{
    if (verify_main_loop_failure_is_terminal() != 0) {
        return 1;
    }
    if (verify_wakeup_failure_is_terminal() != 0) {
        return 1;
    }
    if (verify_command_path_does_not_swallow_runtime_failure() != 0) {
        return 1;
    }
    if (verify_released_context_is_rejected_by_scheduler_boundary() != 0) {
        return 1;
    }
    return 0;
}
