#include "tests/test_support.h"
#include "src/application/coordinators/system_context_private.h"

static int verify_main_loop_failure_is_terminal(void)
{
    controller_runtime_state_view_t controller_runtime_state_view;
    simulated_driver_context_t driver_context;
    system_context_t system_context;
    controller_scheduler_t *controller_scheduler;
    operation_result_t result;

    test_setup_system_context(&system_context, &driver_context);
    controller_scheduler = test_create_scheduler(&system_context, 100ul);
    TEST_ASSERT(controller_scheduler != 0);

    controller_scheduler_linux_test_set_failpoint(controller_scheduler,
        CONTROLLER_SCHEDULER_LINUX_TEST_FAIL_MAIN_LOOP_RUN,
        true);
    result = controller_scheduler_linux_test_inject_period(controller_scheduler, 1u);
    TEST_ASSERT(!result.ok);
    result = controller_scheduler_read_view(controller_scheduler, &controller_runtime_state_view);
    TEST_ASSERT(result.ok);
    TEST_ASSERT(controller_runtime_state_view.runtime_state == CONTROLLER_SCHEDULER_RUNTIME_STATE_FAILED);
    TEST_ASSERT(strcmp(controller_runtime_state_view.metrics.last_error_reason, "main_loop_run_failed") == 0);

    controller_scheduler_linux_destroy(controller_scheduler);
    return 0;
}

static int verify_wakeup_failure_is_terminal(void)
{
    controller_runtime_state_view_t controller_runtime_state_view;
    simulated_driver_context_t driver_context;
    system_context_t system_context;
    controller_scheduler_t *controller_scheduler;
    operation_result_t result;

    test_setup_system_context(&system_context, &driver_context);
    controller_scheduler = test_create_scheduler(&system_context, 100ul);
    TEST_ASSERT(controller_scheduler != 0);

    controller_scheduler_linux_test_set_failpoint(controller_scheduler,
        CONTROLLER_SCHEDULER_LINUX_TEST_FAIL_WAKEUP_WRITE,
        true);
    result = controller_scheduler_request_stop(controller_scheduler);
    TEST_ASSERT(!result.ok);
    result = controller_scheduler_read_view(controller_scheduler, &controller_runtime_state_view);
    TEST_ASSERT(result.ok);
    TEST_ASSERT(controller_runtime_state_view.runtime_state == CONTROLLER_SCHEDULER_RUNTIME_STATE_FAILED);
    TEST_ASSERT(strcmp(controller_runtime_state_view.metrics.last_error_reason, "wakeup_write_failed") == 0);

    controller_scheduler_linux_destroy(controller_scheduler);
    return 0;
}

static int verify_command_path_does_not_swallow_runtime_failure(void)
{
    controller_runtime_state_view_t controller_runtime_state_view;
    simulated_driver_context_t driver_context;
    system_context_t system_context;
    controller_scheduler_t *controller_scheduler;
    char response_line[512];
    operation_result_t result;

    test_setup_system_context(&system_context, &driver_context);
    result = test_load_runtime_program_from_fixture(&system_context,
        "tests/fixtures/wash_step_control/program_v1_valid.json",
        0);
    TEST_ASSERT(result.ok);
    controller_scheduler = test_create_scheduler(&system_context, 100ul);
    TEST_ASSERT(controller_scheduler != 0);

    result = controller_scheduler_linux_test_inject_command(controller_scheduler,
        "fault E_STOP idle-fault",
        response_line,
        sizeof(response_line));
    TEST_ASSERT(result.ok);
    TEST_ASSERT(strcmp(system_context.last_result_code, "accepted") == 0);

    controller_scheduler_linux_test_set_failpoint(controller_scheduler,
        CONTROLLER_SCHEDULER_LINUX_TEST_FAIL_MAIN_LOOP_RUN,
        true);
    result = controller_scheduler_linux_test_inject_command(controller_scheduler,
        "start wash_step_control_v1",
        response_line,
        sizeof(response_line));
    TEST_ASSERT(!result.ok);
    result = controller_scheduler_read_view(controller_scheduler, &controller_runtime_state_view);
    TEST_ASSERT(result.ok);
    TEST_ASSERT(controller_runtime_state_view.runtime_state == CONTROLLER_SCHEDULER_RUNTIME_STATE_FAILED);
    TEST_ASSERT(strcmp(controller_runtime_state_view.metrics.last_error_reason, "main_loop_run_failed") == 0);

    controller_scheduler_linux_destroy(controller_scheduler);
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
    return 0;
}
