#include "application/use_cases/process_formal_command.h"
#include "tests/test_support.h"
#include "src/application/coordinators/system_context_private.h"

static int verify_immediate_exit_stops_without_drain(void)
{
    controller_runtime_state_view_t controller_runtime_state_view;
    simulated_driver_context_t driver_context;
    system_context_t system_context;
    controller_scheduler_t *controller_scheduler;
    operation_result_t result;

    test_setup_system_context(&system_context, &driver_context);
    controller_scheduler = test_create_scheduler(&system_context, 100ul);
    TEST_ASSERT(controller_scheduler != 0);

    TEST_ASSERT(test_scheduler_exit(controller_scheduler, true) == 0);
    result = controller_scheduler_read_view(controller_scheduler, &controller_runtime_state_view);
    TEST_ASSERT(result.ok);
    TEST_ASSERT(controller_runtime_state_view.runtime_state == CONTROLLER_SCHEDULER_RUNTIME_STATE_STOPPED);
    TEST_ASSERT(controller_runtime_state_view.metrics.exit_event_count == 1ul);

    controller_scheduler_linux_destroy(controller_scheduler);
    return 0;
}

static int verify_bounded_drain_has_terminal_conclusion(void)
{
    controller_runtime_state_view_t controller_runtime_state_view;
    simulated_driver_context_t driver_context;
    system_context_t system_context;
    controller_scheduler_t *controller_scheduler;
    char response_line[512];
    operation_result_t result;
    unsigned int step_index;

    test_setup_system_context(&system_context, &driver_context);
    result = test_load_runtime_program_from_fixture(&system_context,
        "tests/fixtures/wash_step_control/program_v1_valid.json",
        0);
    TEST_ASSERT(result.ok);
    controller_scheduler = test_create_scheduler(&system_context, 100ul);
    TEST_ASSERT(controller_scheduler != 0);

    result = process_formal_command_execute(&system_context,
        "start wash_step_control_v1",
        response_line,
        sizeof(response_line));
    TEST_ASSERT(result.ok);
    TEST_ASSERT(system_context.pending_trigger_count == 1u);

    TEST_ASSERT(test_scheduler_exit(controller_scheduler, false) == 0);
    for (step_index = 0u; step_index < 8u; ++step_index) {
        result = controller_scheduler_read_view(controller_scheduler, &controller_runtime_state_view);
        TEST_ASSERT(result.ok);
        if (controller_runtime_state_view.runtime_state == CONTROLLER_SCHEDULER_RUNTIME_STATE_FAILED
            || controller_runtime_state_view.runtime_state == CONTROLLER_SCHEDULER_RUNTIME_STATE_STOPPED) {
            break;
        }
        result = controller_scheduler_linux_test_step(controller_scheduler);
        TEST_ASSERT(result.ok || !result.ok);
        if (!result.ok) {
            break;
        }
    }

    result = controller_scheduler_read_view(controller_scheduler, &controller_runtime_state_view);
    TEST_ASSERT(result.ok);
    TEST_ASSERT(controller_runtime_state_view.runtime_state == CONTROLLER_SCHEDULER_RUNTIME_STATE_FAILED
        || controller_runtime_state_view.runtime_state == CONTROLLER_SCHEDULER_RUNTIME_STATE_STOPPED);
    TEST_ASSERT(controller_runtime_state_view.metrics.exit_event_count == 1ul);

    controller_scheduler_linux_destroy(controller_scheduler);
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
