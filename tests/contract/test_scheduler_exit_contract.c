#include "application/use_cases/process_formal_command.h"
#include "tests/test_support.h"
#include "src/application/coordinators/device_runtime_private.h"

static int verify_immediate_exit_stops_without_drain(void)
{
    scheduler_state_view_t app_state_view;
    simulated_driver_context_t driver_context;
    scheduler_t *scheduler;
    operation_result_t result;

    test_setup_system_context( &driver_context);
    scheduler = test_create_scheduler( 100ul);
    TEST_ASSERT(scheduler != 0);

    TEST_ASSERT(test_scheduler_exit(scheduler, true) == 0);
    result = scheduler_read_view(scheduler, &app_state_view);
    TEST_ASSERT(result.ok);
    TEST_ASSERT(app_state_view.runtime_state == SCHEDULER_RUNTIME_STATE_STOPPED);
    TEST_ASSERT(app_state_view.metrics.exit_event_count == 1ul);

    test_release_system_context();
    return 0;
}

static int verify_bounded_drain_has_terminal_conclusion(void)
{
    scheduler_state_view_t app_state_view;
    simulated_driver_context_t driver_context;
    scheduler_t *scheduler;
    char response_line[512];
    operation_result_t result;
    unsigned int step_index;

    test_setup_system_context( &driver_context);
    result = test_load_runtime_program_from_fixture(
        "tests/fixtures/wash_step_control/program_v1_valid.json",
        0);
    TEST_ASSERT(result.ok);
    scheduler = test_create_scheduler( 100ul);
    TEST_ASSERT(scheduler != 0);

    result = test_homing_system_and_flush();
    TEST_ASSERT(result.ok);
    result = process_formal_command_execute(
        "start wash_step_control_v1",
        response_line,
        sizeof(response_line));
    TEST_ASSERT(result.ok);
    TEST_ASSERT(device_runtime_private_runtime_mutable()->pending_trigger_count == 1u);

    TEST_ASSERT(test_scheduler_exit(scheduler, false) == 0);
    for (step_index = 0u; step_index < 8u; ++step_index) {
        result = scheduler_read_view(scheduler, &app_state_view);
        TEST_ASSERT(result.ok);
        if (app_state_view.runtime_state == SCHEDULER_RUNTIME_STATE_FAILED
            || app_state_view.runtime_state == SCHEDULER_RUNTIME_STATE_STOPPED) {
            break;
        }
        result = scheduler_linux_test_step(scheduler);
        TEST_ASSERT(result.ok || !result.ok);
        if (!result.ok) {
            break;
        }
    }

    result = scheduler_read_view(scheduler, &app_state_view);
    TEST_ASSERT(result.ok);
    TEST_ASSERT(app_state_view.runtime_state == SCHEDULER_RUNTIME_STATE_FAILED
        || app_state_view.runtime_state == SCHEDULER_RUNTIME_STATE_STOPPED);
    TEST_ASSERT(app_state_view.metrics.exit_event_count == 1ul);

    test_release_system_context();
    return 0;
}

int main(void)
{
    if (verify_immediate_exit_stops_without_drain() != 0) {
        return 1;
    }
    if (verify_bounded_drain_has_terminal_conclusion() != 0) {
        return 1;
    }
    return 0;
}

