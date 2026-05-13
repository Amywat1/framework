#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <unistd.h>

#include "platform/linux/controller_scheduler_linux.h"
#include "tests/test_support.h"
#include "src/application/coordinators/system_context_private.h"

static int verify_partial_command_does_not_block_scheduler(void)
{
    controller_runtime_state_view_t controller_runtime_state_view;
    controller_scheduler_config_t controller_scheduler_config;
    simulated_driver_context_t driver_context;
    controller_runtime_t *controller_runtime;
    system_context_t system_context;
    controller_scheduler_t *controller_scheduler;
    FILE *input_file;
    FILE *output_file;
    int pipe_fds[2];
    operation_result_t result;

    TEST_ASSERT(pipe(pipe_fds) == 0);
    input_file = fdopen(pipe_fds[0], "r");
    TEST_ASSERT(input_file != 0);
    output_file = tmpfile();
    TEST_ASSERT(output_file != 0);

    memset(&controller_scheduler_config, 0, sizeof(controller_scheduler_config));
    controller_scheduler_config.control_period_ms = 100ul;
    controller_scheduler_config.command_event_source_enabled = true;
    controller_scheduler_config.notification_event_source_enabled = false;
    controller_scheduler_config.exit_event_source_enabled = false;
    controller_scheduler_config.exit_mode = CONTROLLER_SCHEDULER_EXIT_MODE_BOUNDED_DRAIN;
    controller_scheduler_config.bounded_drain_ticks = 4u;
    controller_scheduler_config.max_triggers_per_tick = 1u;
    controller_scheduler_config.overrun_warning_threshold_ms = 100ul;
    controller_scheduler_config.observability_enabled = true;

    result = test_create_runtime_with_overrides(&controller_runtime,
        &driver_context,
        &controller_scheduler_config,
        input_file,
        output_file,
        0,
        "./configs",
        "./runtime/logs/test_events.log");
    TEST_ASSERT(result.ok);

    result = test_runtime_system_context(controller_runtime, &system_context);
    TEST_ASSERT(result.ok);
    controller_scheduler = test_runtime_scheduler(controller_runtime);
    TEST_ASSERT(controller_scheduler != 0);

    result = test_load_runtime_program_from_fixture(system_context,
        "tests/fixtures/wash_step_control/program_v1_valid.json",
        0);
    TEST_ASSERT(result.ok);

    result = controller_scheduler_read_view(controller_scheduler, &controller_runtime_state_view);
    TEST_ASSERT(result.ok);
    TEST_ASSERT(controller_runtime_state_view.command_source_state == CONTROLLER_SCHEDULER_EVENT_SOURCE_ENABLED);
    TEST_ASSERT(controller_runtime_state_view.notification_source_state == CONTROLLER_SCHEDULER_EVENT_SOURCE_DISABLED);
    TEST_ASSERT(controller_runtime_state_view.exit_source_state == CONTROLLER_SCHEDULER_EVENT_SOURCE_DISABLED);

    TEST_ASSERT(write(pipe_fds[1], "homing\nstart wash_step_control_v1", 33) == 33);
    result = controller_scheduler_linux_test_poll_once(controller_scheduler);
    TEST_ASSERT(result.ok);
    TEST_ASSERT(system_context_private_runtime(system_context)->wash_session.session_state != SESSION_STATE_RUNNING);
    TEST_ASSERT(system_context_private_runtime(system_context)->pending_trigger_count == 0u);
    TEST_ASSERT(system_context_private_runtime(system_context)->current_time_ms == 0ul);

    result = controller_scheduler_linux_test_inject_period(controller_scheduler, 1u);
    TEST_ASSERT(result.ok);
    TEST_ASSERT(system_context_private_runtime(system_context)->current_time_ms == 100ul);
    TEST_ASSERT(system_context_private_runtime(system_context)->wash_session.session_state != SESSION_STATE_RUNNING);

    TEST_ASSERT(write(pipe_fds[1], "\n", 1) == 1);
    result = controller_scheduler_linux_test_poll_once(controller_scheduler);
    TEST_ASSERT(result.ok);
    TEST_ASSERT(system_context_private_runtime(system_context)->wash_session.session_state == SESSION_STATE_RUNNING);

    result = controller_runtime_destroy(controller_runtime);
    TEST_ASSERT(result.ok);
    fclose(output_file);
    fclose(input_file);
    close(pipe_fds[1]);
    return 0;
}

int main(void)
{
    return verify_partial_command_does_not_block_scheduler();
}
