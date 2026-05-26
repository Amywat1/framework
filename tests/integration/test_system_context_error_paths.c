#include <string.h>

#include "application/use_cases/process_formal_command.h"
#include "application/use_cases/process_wash_trigger.h"
#include "application/use_cases/query_wash_session_status.h"
#include "tests/test_support.h"
#include "src/application/coordinators/device_runtime_private.h"

static int verify_null_and_non_runtime_handles_fail_formally(void)
{
    char fake_storage;
    device_runtime_t fake_context;
    device_runtime_t invalid_context;
    wash_session_status_view_t wash_session_status_view;
    wash_trigger_event_t wash_trigger_event;
    operation_result_t result;

    result = device_runtime_acquire(0);
    TEST_ASSERT(!result.ok);
    TEST_ASSERT(result.error_code == ERROR_CODE_INVALID_ARGUMENT);

    memset(&invalid_context, 0, sizeof(invalid_context));

    result = device_runtime_reset(invalid_context);
    TEST_ASSERT(!result.ok);
    TEST_ASSERT(result.error_code == ERROR_CODE_INVALID_ARGUMENT);

    result = device_runtime_release(invalid_context);
    TEST_ASSERT(!result.ok);
    TEST_ASSERT(result.error_code == ERROR_CODE_INVALID_ARGUMENT);

    fake_context = (device_runtime_t)&fake_storage;

    result = device_runtime_reset(fake_context);
    TEST_ASSERT(!result.ok);
    TEST_ASSERT(result.error_code == ERROR_CODE_INVALID_ARGUMENT);

    result = control_tick_run(fake_context);
    TEST_ASSERT(!result.ok);
    TEST_ASSERT(result.error_code == ERROR_CODE_INVALID_ARGUMENT);

    result = query_wash_session_status_execute(fake_context, &wash_session_status_view);
    TEST_ASSERT(!result.ok);
    TEST_ASSERT(result.error_code == ERROR_CODE_INVALID_ARGUMENT);

    wash_trigger_event_init(&wash_trigger_event, TRIGGER_TYPE_STOP, 0, "invalid", "invalid", 0ul);
    result = process_wash_trigger_execute(fake_context, &wash_trigger_event);
    TEST_ASSERT(!result.ok);
    TEST_ASSERT(result.error_code == ERROR_CODE_INVALID_ARGUMENT);
    return 0;
}

static int verify_second_acquire_is_rejected_until_release(void)
{
    device_runtime_t first_handle;
    device_runtime_t second_handle;
    operation_result_t result;

    result = device_runtime_acquire(&first_handle);
    TEST_ASSERT(result.ok);

    result = device_runtime_acquire(&second_handle);
    TEST_ASSERT(!result.ok);
    TEST_ASSERT(result.error_code == ERROR_CODE_RESOURCE_UNAVAILABLE);
    TEST_ASSERT(second_handle == 0);

    result = device_runtime_release(first_handle);
    TEST_ASSERT(result.ok);

    result = device_runtime_acquire(&second_handle);
    TEST_ASSERT(result.ok);
    TEST_ASSERT(second_handle == first_handle);
    result = device_runtime_release(second_handle);
    TEST_ASSERT(result.ok);
    return 0;
}

static int verify_released_handle_is_rejected_by_runtime_paths(void)
{
    scheduler_config_t scheduler_config;
    scheduler_stdio_t scheduler_stdio;
    scheduler_state_view_t app_state_view;
    device_runtime_t released_handle;
    device_runtime_t reacquired_context;
    device_runtime_t system_context;
    simulated_driver_context_t driver_context;
    wash_session_status_view_t wash_session_status_view;
    wash_trigger_event_t wash_trigger_event;
    operation_result_t result;
    scheduler_t *scheduler;
    char response_line[256];

    test_setup_system_context(&system_context, &driver_context);
    released_handle = system_context;
    test_release_system_context(system_context);

    wash_trigger_event_init(&wash_trigger_event, TRIGGER_TYPE_STOP, 0, "released", "released", 0ul);
    result = control_tick_submit_trigger(system_context, &wash_trigger_event);
    TEST_ASSERT(!result.ok);
    TEST_ASSERT(result.error_code == ERROR_CODE_INVALID_STATE);

    result = process_wash_trigger_execute(system_context, &wash_trigger_event);
    TEST_ASSERT(!result.ok);
    TEST_ASSERT(result.error_code == ERROR_CODE_INVALID_STATE);

    memset(response_line, 0, sizeof(response_line));
    result = process_formal_command_execute(system_context, "status", response_line, sizeof(response_line));
    TEST_ASSERT(!result.ok);
    TEST_ASSERT(result.error_code == ERROR_CODE_INVALID_STATE);
    TEST_ASSERT(strstr(response_line, "result=invalid_state") != 0);

    result = query_wash_session_status_execute(system_context, &wash_session_status_view);
    TEST_ASSERT(!result.ok);
    TEST_ASSERT(result.error_code == ERROR_CODE_INVALID_STATE);
    result = test_scheduler_read_bound_view(system_context, &app_state_view);
    TEST_ASSERT(!result.ok);
    TEST_ASSERT(result.error_code == ERROR_CODE_INVALID_STATE);

    memset(&scheduler_config, 0, sizeof(scheduler_config));
    scheduler_config.control_period_ms = 100ul;
    scheduler_config.command_event_source_enabled = true;
    scheduler_config.notification_event_source_enabled = true;
    scheduler_config.exit_event_source_enabled = true;
    scheduler_config.exit_mode = SCHEDULER_EXIT_MODE_BOUNDED_DRAIN;
    scheduler_config.bounded_drain_ticks = 4u;
    scheduler_config.max_triggers_per_tick = 1u;
    scheduler_config.overrun_warning_threshold_ms = 100ul;
    memset(&scheduler_stdio, 0, sizeof(scheduler_stdio));

    scheduler = test_scheduler_create_unbound(system_context,
        &scheduler_config,
        &scheduler_stdio);
    TEST_ASSERT(scheduler == 0);

    result = device_runtime_release(system_context);
    TEST_ASSERT(!result.ok);
    TEST_ASSERT(result.error_code == ERROR_CODE_INVALID_STATE);

    result = device_runtime_acquire(&reacquired_context);
    TEST_ASSERT(result.ok);
    TEST_ASSERT(reacquired_context == released_handle);
    TEST_ASSERT(device_runtime_private_debug_is_in_use(released_handle));

    TEST_ASSERT(device_runtime_current_time_ms(reacquired_context) == 0ul);
    TEST_ASSERT(device_runtime_pending_trigger_count(reacquired_context) == 0u);

    result = test_scheduler_read_bound_view(released_handle, &app_state_view);
    TEST_ASSERT(!result.ok);
    TEST_ASSERT(result.error_code == ERROR_CODE_INVALID_STATE);

    result = device_runtime_release(reacquired_context);
    TEST_ASSERT(result.ok);
    return 0;
}

static int verify_bound_scheduler_blocks_release_and_rebind(void)
{
    scheduler_config_t scheduler_config;
    scheduler_stdio_t scheduler_stdio;
    device_runtime_t system_context;
    simulated_driver_context_t driver_context;
    scheduler_t *scheduler;
    scheduler_t *duplicate_scheduler;
    operation_result_t result;

    test_setup_system_context(&system_context, &driver_context);
    scheduler = test_create_scheduler(system_context, 100ul);
    TEST_ASSERT(scheduler != 0);

    result = device_runtime_release(system_context);
    TEST_ASSERT(!result.ok);
    TEST_ASSERT(result.error_code == ERROR_CODE_INVALID_STATE);

    test_init_scheduler_config(&scheduler_config, 100ul);
    memset(&scheduler_stdio, 0, sizeof(scheduler_stdio));
    duplicate_scheduler = test_scheduler_create_unbound(system_context,
        &scheduler_config,
        &scheduler_stdio);
    TEST_ASSERT(duplicate_scheduler == 0);

    test_release_system_context(system_context);
    return 0;
}

static int read_text_file(const char *path, char *buffer, size_t buffer_size)
{
    FILE *file_handle;
    size_t read_size;

    if (path == 0 || buffer == 0 || buffer_size == 0u) {
        return -1;
    }

    file_handle = fopen(path, "r");
    if (file_handle == 0) {
        return -1;
    }
    read_size = fread(buffer, 1u, buffer_size - 1u, file_handle);
    buffer[read_size] = '\0';
    fclose(file_handle);
    return 0;
}

static int verify_single_instance_file_adapters_rebind_cleanly(void)
{
    const program_repository_port_t *program_repository_port;
    const device_runtime_state_t *runtime;
    simulated_driver_context_t first_driver_context;
    simulated_driver_context_t second_driver_context;
    sensor_port_t first_sensor_port;
    sensor_port_t second_sensor_port;
    actuator_port_t first_actuator_port;
    actuator_port_t second_actuator_port;
    device_runtime_t first_device_runtime;
    device_runtime_t second_device_runtime;
    wash_program_t loaded_program;
    wash_program_t first_runtime_program;
    wash_program_t second_runtime_program;
    operation_result_t result;

    result = device_runtime_acquire(&first_device_runtime);
    TEST_ASSERT(result.ok);

    test_bind_simulated_ports(&first_driver_context, &first_sensor_port, &first_actuator_port);
    device_runtime_set_sensor_port(first_device_runtime, &first_sensor_port);
    device_runtime_set_actuator_port(first_device_runtime, &first_actuator_port);

    result = file_program_repository_init(first_device_runtime, "./configs");
    TEST_ASSERT(result.ok);

    result = json_program_parser_parse("tests/fixtures/wash_step_control/program_v1_valid.json", &first_runtime_program);
    TEST_ASSERT(result.ok);
    file_program_repository_set_runtime_program(first_device_runtime, &first_runtime_program, 101);

    program_repository_port = device_runtime_program_repository_port(first_device_runtime);
    TEST_ASSERT(program_repository_port != 0);
    TEST_ASSERT(program_repository_port->context != 0);

    memset(&loaded_program, 0, sizeof(loaded_program));
    TEST_ASSERT(program_repository_port->load_program(program_repository_port->context,
        first_runtime_program.program_id,
        &loaded_program) == 0);
    TEST_ASSERT(strcmp(loaded_program.program_id, first_runtime_program.program_id) == 0);
    TEST_ASSERT(loaded_program.revision == 101);

    runtime = device_runtime_private_runtime(first_device_runtime);
    TEST_ASSERT(runtime != 0);
    TEST_ASSERT(runtime->program_repository_port.context != 0);

    result = device_runtime_release(first_device_runtime);
    TEST_ASSERT(result.ok);

    result = device_runtime_acquire(&second_device_runtime);
    TEST_ASSERT(result.ok);
    TEST_ASSERT(second_device_runtime == first_device_runtime);

    test_bind_simulated_ports(&second_driver_context, &second_sensor_port, &second_actuator_port);
    device_runtime_set_sensor_port(second_device_runtime, &second_sensor_port);
    device_runtime_set_actuator_port(second_device_runtime, &second_actuator_port);

    result = file_program_repository_init(second_device_runtime, "./configs");
    TEST_ASSERT(result.ok);
    result = json_program_parser_parse("tests/fixtures/wash_step_control/program_v1_valid_json_edge_cases.json",
        &second_runtime_program);
    TEST_ASSERT(result.ok);
    file_program_repository_set_runtime_program(second_device_runtime, &second_runtime_program, 202);

    program_repository_port = device_runtime_program_repository_port(second_device_runtime);
    TEST_ASSERT(program_repository_port != 0);
    TEST_ASSERT(program_repository_port->context != 0);
    memset(&loaded_program, 0, sizeof(loaded_program));
    TEST_ASSERT(program_repository_port->load_program(program_repository_port->context,
        second_runtime_program.program_id,
        &loaded_program) == 0);
    TEST_ASSERT(strcmp(loaded_program.program_id, second_runtime_program.program_id) == 0);
    TEST_ASSERT(loaded_program.revision == 202);

    runtime = device_runtime_private_runtime(second_device_runtime);
    TEST_ASSERT(runtime != 0);
    TEST_ASSERT(runtime->program_repository_port.context != 0);

    result = device_runtime_release(second_device_runtime);
    TEST_ASSERT(result.ok);
    return 0;
}

static int verify_failed_program_import_does_not_bind_runtime_cache(void)
{
    simulated_driver_context_t driver_context;
    device_runtime_t system_context;
    wash_program_t wash_program;
    operation_result_t result;

    test_setup_system_context(&system_context, &driver_context);

    result = test_load_runtime_program_from_fixture(
        system_context,
        "tests/fixtures/wash_step_control/program_v1_invalid_json_syntax.json",
        &wash_program);
    TEST_ASSERT(!result.ok);
    TEST_ASSERT(result.error_code == ERROR_CODE_PARSE_FAILED);
    TEST_ASSERT(test_load_program_via_repository(system_context, "broken_json", &wash_program) != 0);

    result = test_load_runtime_program_from_fixture(
        system_context,
        "tests/fixtures/wash_step_control/program_v1_invalid_stages_schema.json",
        &wash_program);
    TEST_ASSERT(!result.ok);
    TEST_ASSERT(result.error_code == ERROR_CODE_UNSUPPORTED);
    TEST_ASSERT(test_load_program_via_repository(system_context, "legacy_stages_program", &wash_program) != 0);

    test_release_system_context(system_context);
    return 0;
}

int main(void)
{
    if (verify_null_and_non_runtime_handles_fail_formally() != 0) {
        return 1;
    }
    if (verify_second_acquire_is_rejected_until_release() != 0) {
        return 1;
    }
    if (verify_released_handle_is_rejected_by_runtime_paths() != 0) {
        return 1;
    }
    if (verify_bound_scheduler_blocks_release_and_rebind() != 0) {
        return 1;
    }
    if (verify_single_instance_file_adapters_rebind_cleanly() != 0) {
        return 1;
    }
    if (verify_failed_program_import_does_not_bind_runtime_cache() != 0) {
        return 1;
    }
    return 0;
}
