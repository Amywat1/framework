#include <string.h>

#include "application/use_cases/process_formal_command.h"
#include "application/use_cases/process_wash_trigger.h"
#include "application/use_cases/query_wash_session_status.h"
#include "tests/test_support.h"
#include "src/application/coordinators/system_context_private.h"

static int verify_null_and_non_runtime_handles_fail_formally(void)
{
    char fake_storage;
    system_context_t fake_context;
    system_context_t invalid_context;
    wash_session_status_view_t wash_session_status_view;
    wash_trigger_event_t wash_trigger_event;
    operation_result_t result;

    result = system_context_acquire(0);
    TEST_ASSERT(!result.ok);
    TEST_ASSERT(result.error_code == ERROR_CODE_INVALID_ARGUMENT);

    memset(&invalid_context, 0, sizeof(invalid_context));

    result = system_context_reset(invalid_context);
    TEST_ASSERT(!result.ok);
    TEST_ASSERT(result.error_code == ERROR_CODE_INVALID_ARGUMENT);

    result = system_context_release(invalid_context);
    TEST_ASSERT(!result.ok);
    TEST_ASSERT(result.error_code == ERROR_CODE_INVALID_ARGUMENT);

    fake_context = (system_context_t)&fake_storage;

    result = system_context_reset(fake_context);
    TEST_ASSERT(!result.ok);
    TEST_ASSERT(result.error_code == ERROR_CODE_INVALID_ARGUMENT);

    result = main_loop_run(fake_context);
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
    system_context_t first_handle;
    system_context_t second_handle;
    operation_result_t result;

    result = system_context_acquire(&first_handle);
    TEST_ASSERT(result.ok);

    result = system_context_acquire(&second_handle);
    TEST_ASSERT(!result.ok);
    TEST_ASSERT(result.error_code == ERROR_CODE_RESOURCE_UNAVAILABLE);
    TEST_ASSERT(second_handle == 0);

    result = system_context_release(first_handle);
    TEST_ASSERT(result.ok);

    result = system_context_acquire(&second_handle);
    TEST_ASSERT(result.ok);
    TEST_ASSERT(second_handle == first_handle);
    result = system_context_release(second_handle);
    TEST_ASSERT(result.ok);
    return 0;
}

static int verify_released_handle_is_rejected_by_runtime_paths(void)
{
    controller_scheduler_config_t controller_scheduler_config;
    controller_scheduler_stdio_t controller_scheduler_stdio;
    controller_runtime_state_view_t controller_runtime_state_view;
    system_context_t released_handle;
    system_context_t reacquired_context;
    system_context_t system_context;
    simulated_driver_context_t driver_context;
    wash_session_status_view_t wash_session_status_view;
    wash_trigger_event_t wash_trigger_event;
    operation_result_t result;
    controller_scheduler_t *controller_scheduler;
    char response_line[256];

    test_setup_system_context(&system_context, &driver_context);
    released_handle = system_context;
    test_release_system_context(system_context);

    wash_trigger_event_init(&wash_trigger_event, TRIGGER_TYPE_STOP, 0, "released", "released", 0ul);
    result = main_loop_submit_trigger(system_context, &wash_trigger_event);
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
    result = controller_scheduler_read_context_view(system_context, &controller_runtime_state_view);
    TEST_ASSERT(!result.ok);
    TEST_ASSERT(result.error_code == ERROR_CODE_INVALID_STATE);

    memset(&controller_scheduler_config, 0, sizeof(controller_scheduler_config));
    controller_scheduler_config.control_period_ms = 100ul;
    controller_scheduler_config.command_event_source_enabled = true;
    controller_scheduler_config.notification_event_source_enabled = true;
    controller_scheduler_config.exit_event_source_enabled = true;
    controller_scheduler_config.exit_mode = CONTROLLER_SCHEDULER_EXIT_MODE_BOUNDED_DRAIN;
    controller_scheduler_config.bounded_drain_ticks = 4u;
    controller_scheduler_config.max_triggers_per_tick = 1u;
    controller_scheduler_config.overrun_warning_threshold_ms = 100ul;
    controller_scheduler_config.observability_enabled = true;
    memset(&controller_scheduler_stdio, 0, sizeof(controller_scheduler_stdio));

    controller_scheduler = controller_scheduler_create(system_context,
        &controller_scheduler_config,
        &controller_scheduler_stdio);
    TEST_ASSERT(controller_scheduler == 0);

    result = system_context_release(system_context);
    TEST_ASSERT(!result.ok);
    TEST_ASSERT(result.error_code == ERROR_CODE_INVALID_STATE);

    result = system_context_acquire(&reacquired_context);
    TEST_ASSERT(result.ok);
    TEST_ASSERT(reacquired_context == released_handle);
    TEST_ASSERT(system_context_private_debug_is_in_use(released_handle));

    TEST_ASSERT(system_context_current_time_ms(reacquired_context) == 0ul);
    TEST_ASSERT(system_context_pending_trigger_count(reacquired_context) == 0u);

    result = controller_scheduler_read_context_view(released_handle, &controller_runtime_state_view);
    TEST_ASSERT(!result.ok);
    TEST_ASSERT(result.error_code == ERROR_CODE_INVALID_STATE);

    result = system_context_release(reacquired_context);
    TEST_ASSERT(result.ok);
    return 0;
}

static int verify_bound_scheduler_blocks_release_and_rebind(void)
{
    controller_scheduler_config_t controller_scheduler_config;
    controller_scheduler_stdio_t controller_scheduler_stdio;
    system_context_t system_context;
    simulated_driver_context_t driver_context;
    controller_scheduler_t *controller_scheduler;
    controller_scheduler_t *duplicate_scheduler;
    operation_result_t result;

    test_setup_system_context(&system_context, &driver_context);
    controller_scheduler = test_create_scheduler(system_context, 100ul);
    TEST_ASSERT(controller_scheduler != 0);

    result = system_context_release(system_context);
    TEST_ASSERT(!result.ok);
    TEST_ASSERT(result.error_code == ERROR_CODE_INVALID_STATE);

    test_init_scheduler_config(&controller_scheduler_config, 100ul);
    memset(&controller_scheduler_stdio, 0, sizeof(controller_scheduler_stdio));
    duplicate_scheduler = controller_scheduler_create(system_context,
        &controller_scheduler_config,
        &controller_scheduler_stdio);
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
    const char *first_log_path = "./runtime/logs/test_events_ctx_a.log";
    const char *second_log_path = "./runtime/logs/test_events_ctx_b.log";
    char first_log_buffer[256];
    char second_log_buffer[256];
    const program_repository_port_t *program_repository_port;
    const system_context_runtime_t *runtime;
    simulated_driver_context_t first_driver_context;
    simulated_driver_context_t second_driver_context;
    sensor_port_t first_sensor_port;
    sensor_port_t second_sensor_port;
    actuator_port_t first_actuator_port;
    actuator_port_t second_actuator_port;
    system_context_t first_system_context;
    system_context_t second_system_context;
    wash_program_t loaded_program;
    wash_program_t first_runtime_program;
    wash_program_t second_runtime_program;
    operation_result_t result;

    remove(first_log_path);
    remove(second_log_path);
    result = system_context_acquire(&first_system_context);
    TEST_ASSERT(result.ok);

    test_bind_simulated_ports(&first_driver_context, &first_sensor_port, &first_actuator_port);
    system_context_set_sensor_port(first_system_context, &first_sensor_port);
    system_context_set_actuator_port(first_system_context, &first_actuator_port);

    result = file_program_repository_init(first_system_context, "./configs");
    TEST_ASSERT(result.ok);
    result = file_event_logger_init(first_system_context, first_log_path);
    TEST_ASSERT(result.ok);

    result = json_program_parser_parse("tests/fixtures/wash_step_control/program_v1_valid.json", &first_runtime_program);
    TEST_ASSERT(result.ok);
    file_program_repository_set_runtime_program(first_system_context, &first_runtime_program, 101);

    program_repository_port = system_context_program_repository_port(first_system_context);
    TEST_ASSERT(program_repository_port != 0);
    TEST_ASSERT(program_repository_port->context != 0);

    memset(&loaded_program, 0, sizeof(loaded_program));
    TEST_ASSERT(program_repository_port->load_program(program_repository_port->context,
        first_runtime_program.program_id,
        &loaded_program) == 0);
    TEST_ASSERT(strcmp(loaded_program.program_id, first_runtime_program.program_id) == 0);
    TEST_ASSERT(loaded_program.revision == 101);

    runtime = system_context_private_runtime(first_system_context);
    TEST_ASSERT(runtime != 0);
    TEST_ASSERT(runtime->event_logger_port.context != 0);
    TEST_ASSERT(runtime->event_logger_port.log_message != 0);
    TEST_ASSERT(runtime->event_logger_port.log_message(runtime->event_logger_port.context,
        TRIGGER_TYPE_START,
        "ctx-a") == 0);

    TEST_ASSERT(read_text_file(first_log_path, first_log_buffer, sizeof(first_log_buffer)) == 0);
    TEST_ASSERT(strstr(first_log_buffer, "ctx-a") != 0);

    result = system_context_release(first_system_context);
    TEST_ASSERT(result.ok);

    result = system_context_acquire(&second_system_context);
    TEST_ASSERT(result.ok);
    TEST_ASSERT(second_system_context != first_system_context);

    test_bind_simulated_ports(&second_driver_context, &second_sensor_port, &second_actuator_port);
    system_context_set_sensor_port(second_system_context, &second_sensor_port);
    system_context_set_actuator_port(second_system_context, &second_actuator_port);

    result = file_program_repository_init(second_system_context, "./configs");
    TEST_ASSERT(result.ok);
    result = file_event_logger_init(second_system_context, second_log_path);
    TEST_ASSERT(result.ok);
    result = json_program_parser_parse("tests/fixtures/wash_step_control/program_v1_valid_json_edge_cases.json",
        &second_runtime_program);
    TEST_ASSERT(result.ok);
    file_program_repository_set_runtime_program(second_system_context, &second_runtime_program, 202);

    program_repository_port = system_context_program_repository_port(second_system_context);
    TEST_ASSERT(program_repository_port != 0);
    TEST_ASSERT(program_repository_port->context != 0);
    memset(&loaded_program, 0, sizeof(loaded_program));
    TEST_ASSERT(program_repository_port->load_program(program_repository_port->context,
        second_runtime_program.program_id,
        &loaded_program) == 0);
    TEST_ASSERT(strcmp(loaded_program.program_id, second_runtime_program.program_id) == 0);
    TEST_ASSERT(loaded_program.revision == 202);

    runtime = system_context_private_runtime(second_system_context);
    TEST_ASSERT(runtime != 0);
    TEST_ASSERT(runtime->event_logger_port.context != 0);
    TEST_ASSERT(runtime->event_logger_port.log_message != 0);
    TEST_ASSERT(runtime->event_logger_port.log_message(runtime->event_logger_port.context,
        TRIGGER_TYPE_STOP,
        "ctx-b") == 0);

    TEST_ASSERT(read_text_file(second_log_path, second_log_buffer, sizeof(second_log_buffer)) == 0);
    TEST_ASSERT(strstr(second_log_buffer, "ctx-b") != 0);
    TEST_ASSERT(strstr(second_log_buffer, "ctx-a") == 0);

    result = system_context_release(second_system_context);
    TEST_ASSERT(result.ok);
    return 0;
}

static int verify_failed_program_import_does_not_bind_runtime_cache(void)
{
    simulated_driver_context_t driver_context;
    system_context_t system_context;
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
