#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <unistd.h>

#include "platform/linux/scheduler_linux.h"
#include "tests/test_support.h"
#include "src/application/coordinators/device_runtime_private.h"

static int verify_partial_command_does_not_block_scheduler(void)
{
    scheduler_state_view_t app_state_view;
    scheduler_config_t scheduler_config;
    simulated_driver_context_t driver_context;
    app_t *app;
    device_runtime_t system_context;
    scheduler_t *scheduler;
    FILE *input_file;
    FILE *output_file;
    int pipe_fds[2];
    operation_result_t result;

    TEST_ASSERT(pipe(pipe_fds) == 0);
    input_file = fdopen(pipe_fds[0], "r");
    TEST_ASSERT(input_file != 0);
    output_file = tmpfile();
    TEST_ASSERT(output_file != 0);

    memset(&scheduler_config, 0, sizeof(scheduler_config));
    scheduler_config.control_period_ms = 100ul;
    scheduler_config.command_event_source_enabled = true;
    scheduler_config.notification_event_source_enabled = false;
    scheduler_config.exit_event_source_enabled = false;
    scheduler_config.exit_mode = SCHEDULER_EXIT_MODE_BOUNDED_DRAIN;
    scheduler_config.bounded_drain_ticks = 4u;
    scheduler_config.max_triggers_per_tick = 1u;
    scheduler_config.overrun_warning_threshold_ms = 100ul;

    result = test_create_runtime_with_overrides(&app,
        &driver_context,
        &scheduler_config,
        input_file,
        output_file,
        0, "./runtime/logs/test_events.log");
    TEST_ASSERT(result.ok);

    result = test_runtime_system_context(app, &system_context);
    TEST_ASSERT(result.ok);
    scheduler = test_runtime_scheduler(app);
    TEST_ASSERT(scheduler != 0);

    result = test_load_runtime_program_from_fixture(system_context,
        "tests/fixtures/wash_step_control/program_v1_valid.json",
        0);
    TEST_ASSERT(result.ok);

    result = scheduler_read_view(scheduler, &app_state_view);
    TEST_ASSERT(result.ok);
    TEST_ASSERT(app_state_view.command_source_state == SCHEDULER_EVENT_SOURCE_ENABLED);
    TEST_ASSERT(app_state_view.notification_source_state == SCHEDULER_EVENT_SOURCE_DISABLED);
    TEST_ASSERT(app_state_view.exit_source_state == SCHEDULER_EVENT_SOURCE_DISABLED);

    TEST_ASSERT(write(pipe_fds[1], "homing\nstart wash_step_control_v1", 33) == 33);
    result = scheduler_linux_test_poll_once(scheduler);
    TEST_ASSERT(result.ok);
    TEST_ASSERT(device_runtime_private_runtime(system_context)->wash_session.session_state != SESSION_STATE_RUNNING);
    TEST_ASSERT(device_runtime_private_runtime(system_context)->pending_trigger_count == 0u);
    TEST_ASSERT(device_runtime_private_runtime(system_context)->current_time_ms == 0ul);

    result = scheduler_linux_test_inject_period(scheduler, 1u);
    TEST_ASSERT(result.ok);
    TEST_ASSERT(device_runtime_private_runtime(system_context)->current_time_ms == 100ul);
    TEST_ASSERT(device_runtime_private_runtime(system_context)->wash_session.session_state != SESSION_STATE_RUNNING);

    TEST_ASSERT(write(pipe_fds[1], "\n", 1) == 1);
    result = scheduler_linux_test_poll_once(scheduler);
    TEST_ASSERT(result.ok);
    TEST_ASSERT(device_runtime_private_runtime(system_context)->wash_session.session_state == SESSION_STATE_RUNNING);

    result = app_destroy(app);
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
