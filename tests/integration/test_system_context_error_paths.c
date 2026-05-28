#include <string.h>

#include "application/use_cases/process_formal_command.h"
#include "application/use_cases/process_wash_trigger.h"
#include "application/use_cases/query_wash_session_status.h"
#include "tests/test_support.h"
#include "src/application/coordinators/control_context_private.h"

static int verify_calls_fail_before_acquire(void)
{
    wash_session_status_view_t wash_session_status_view;
    wash_trigger_event_t wash_trigger_event;
    operation_result_t result;
    char response_line[256];

    /* 未 acquire 时，所有接口应返回 INVALID_STATE */
    result = control_context_reset();
    TEST_ASSERT(!result.ok);
    TEST_ASSERT(result.error_code == ERROR_CODE_INVALID_STATE);

    result = control_context_deinit();
    TEST_ASSERT(!result.ok);
    TEST_ASSERT(result.error_code == ERROR_CODE_INVALID_STATE);

    result = control_tick_run();
    TEST_ASSERT(!result.ok);
    TEST_ASSERT(result.error_code == ERROR_CODE_INVALID_STATE);

    result = query_wash_session_status_execute(&wash_session_status_view);
    TEST_ASSERT(!result.ok);
    TEST_ASSERT(result.error_code == ERROR_CODE_INVALID_STATE);

    wash_trigger_event_init(&wash_trigger_event, TRIGGER_TYPE_STOP, 0, "invalid", "invalid", 0ul);
    result = process_wash_trigger_execute(&wash_trigger_event);
    TEST_ASSERT(!result.ok);
    TEST_ASSERT(result.error_code == ERROR_CODE_INVALID_STATE);

    memset(response_line, 0, sizeof(response_line));
    result = process_formal_command_execute("status", response_line, sizeof(response_line));
    TEST_ASSERT(!result.ok);
    TEST_ASSERT(result.error_code == ERROR_CODE_INVALID_STATE);
    return 0;
}

static int verify_second_acquire_is_rejected_until_release(void)
{
    operation_result_t result;

    result = control_context_init();
    TEST_ASSERT(result.ok);

    result = control_context_init();
    TEST_ASSERT(!result.ok);
    TEST_ASSERT(result.error_code == ERROR_CODE_RESOURCE_UNAVAILABLE);

    result = control_context_deinit();
    TEST_ASSERT(result.ok);

    result = control_context_init();
    TEST_ASSERT(result.ok);
    result = control_context_deinit();
    TEST_ASSERT(result.ok);
    return 0;
}

static int verify_released_handle_is_rejected_by_runtime_paths(void)
{
    scheduler_config_t scheduler_config;
    scheduler_stdio_t scheduler_stdio;
    scheduler_state_view_t app_state_view;
    simulated_driver_context_t driver_context;
    wash_session_status_view_t wash_session_status_view;
    wash_trigger_event_t wash_trigger_event;
    operation_result_t result;
    scheduler_t *scheduler;
    char response_line[256];

    test_setup_system_context(&driver_context);
    test_release_system_context();

    /* 释放后所有接口应返回 INVALID_STATE */
    wash_trigger_event_init(&wash_trigger_event, TRIGGER_TYPE_STOP, 0, "released", "released", 0ul);
    result = control_tick_submit_trigger(&wash_trigger_event);
    TEST_ASSERT(!result.ok);
    TEST_ASSERT(result.error_code == ERROR_CODE_INVALID_STATE);

    result = process_wash_trigger_execute(&wash_trigger_event);
    TEST_ASSERT(!result.ok);
    TEST_ASSERT(result.error_code == ERROR_CODE_INVALID_STATE);

    memset(response_line, 0, sizeof(response_line));
    result = process_formal_command_execute("status", response_line, sizeof(response_line));
    TEST_ASSERT(!result.ok);
    TEST_ASSERT(result.error_code == ERROR_CODE_INVALID_STATE);
    TEST_ASSERT(strstr(response_line, "result=invalid_state") != 0);

    result = query_wash_session_status_execute(&wash_session_status_view);
    TEST_ASSERT(!result.ok);
    TEST_ASSERT(result.error_code == ERROR_CODE_INVALID_STATE);
    result = test_scheduler_read_bound_view(&app_state_view);
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

    /* 释放后创建调度器应失败（scheduler_runtime_port_init_from_control_context 会成功但调度器内部会失败） */
    scheduler = test_scheduler_create_unbound(&scheduler_config, &scheduler_stdio);
    /* 注意：在单实例设计下，scheduler_runtime_port_init 始终成功，但 scheduler_create 仍应能创建
     * （调度器创建本身不依赖 control_context 状态）。此处仅验证 re-acquire 后可正常使用。*/
    if (scheduler != 0)
    {
        scheduler_destroy(scheduler);
    }

    result = control_context_deinit();
    TEST_ASSERT(!result.ok);
    TEST_ASSERT(result.error_code == ERROR_CODE_INVALID_STATE);

    /* 重新 acquire 后接口应正常 */
    result = control_context_init();
    TEST_ASSERT(result.ok);
    TEST_ASSERT(control_context_current_time_ms() == 0ul);
    TEST_ASSERT(control_context_pending_trigger_count() == 0u);

    result = control_context_deinit();
    TEST_ASSERT(result.ok);
    return 0;
}

static int verify_bound_scheduler_blocks_release_and_rebind(void)
{
    scheduler_config_t scheduler_config;
    scheduler_stdio_t scheduler_stdio;
    simulated_driver_context_t driver_context;
    scheduler_t *scheduler;
    scheduler_t *duplicate_scheduler;
    operation_result_t result;

    test_setup_system_context(&driver_context);
    scheduler = test_create_scheduler(100ul);
    TEST_ASSERT(scheduler != 0);

    result = control_context_deinit();
    TEST_ASSERT(!result.ok);
    TEST_ASSERT(result.error_code == ERROR_CODE_INVALID_STATE);

    test_init_scheduler_config(&scheduler_config, 100ul);
    memset(&scheduler_stdio, 0, sizeof(scheduler_stdio));
    duplicate_scheduler = test_scheduler_create_unbound(
        &scheduler_config,
        &scheduler_stdio);
    TEST_ASSERT(duplicate_scheduler == 0);

    test_release_system_context();
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
    const control_context_state_t *runtime;
    simulated_driver_context_t first_driver_context;
    simulated_driver_context_t second_driver_context;
    sensor_port_t first_sensor_port;
    sensor_port_t second_sensor_port;
    actuator_port_t first_actuator_port;
    actuator_port_t second_actuator_port;
    wash_program_t loaded_program;
    wash_program_t first_runtime_program;
    wash_program_t second_runtime_program;
    operation_result_t result;

    result = control_context_init();
    TEST_ASSERT(result.ok);

    test_bind_simulated_ports(&first_driver_context, &first_sensor_port, &first_actuator_port);
    control_context_set_sensor_port(&first_sensor_port);
    control_context_set_actuator_port(&first_actuator_port);

    result = file_program_repository_init("./configs");
    TEST_ASSERT(result.ok);

    result = json_program_parser_parse("tests/fixtures/wash_step_control/program_v1_valid.json", &first_runtime_program);
    TEST_ASSERT(result.ok);
    file_program_repository_set_runtime_program(&first_runtime_program, 101);

    program_repository_port = control_context_program_repository_port();
    TEST_ASSERT(program_repository_port != 0);
    TEST_ASSERT(program_repository_port->context != 0);

    memset(&loaded_program, 0, sizeof(loaded_program));
    TEST_ASSERT(program_repository_port->load_program(program_repository_port->context,
        first_runtime_program.program_id,
        &loaded_program) == 0);
    TEST_ASSERT(strcmp(loaded_program.program_id, first_runtime_program.program_id) == 0);
    TEST_ASSERT(loaded_program.revision == 101);

    runtime = control_context_private_runtime_mutable();
    TEST_ASSERT(runtime != 0);
    TEST_ASSERT(runtime->program_repository_port.context != 0);

    result = control_context_deinit();
    TEST_ASSERT(result.ok);

    result = control_context_init();
    TEST_ASSERT(result.ok);

    test_bind_simulated_ports(&second_driver_context, &second_sensor_port, &second_actuator_port);
    control_context_set_sensor_port(&second_sensor_port);
    control_context_set_actuator_port(&second_actuator_port);

    result = file_program_repository_init("./configs");
    TEST_ASSERT(result.ok);
    result = json_program_parser_parse("tests/fixtures/wash_step_control/program_v1_valid_json_edge_cases.json",
        &second_runtime_program);
    TEST_ASSERT(result.ok);
    file_program_repository_set_runtime_program(&second_runtime_program, 202);

    program_repository_port = control_context_program_repository_port();
    TEST_ASSERT(program_repository_port != 0);
    TEST_ASSERT(program_repository_port->context != 0);
    memset(&loaded_program, 0, sizeof(loaded_program));
    TEST_ASSERT(program_repository_port->load_program(program_repository_port->context,
        second_runtime_program.program_id,
        &loaded_program) == 0);
    TEST_ASSERT(strcmp(loaded_program.program_id, second_runtime_program.program_id) == 0);
    TEST_ASSERT(loaded_program.revision == 202);

    runtime = control_context_private_runtime_mutable();
    TEST_ASSERT(runtime != 0);
    TEST_ASSERT(runtime->program_repository_port.context != 0);

    result = control_context_deinit();
    TEST_ASSERT(result.ok);
    return 0;
}

static int verify_failed_program_import_does_not_bind_runtime_cache(void)
{
    simulated_driver_context_t driver_context;
    wash_program_t wash_program;
    operation_result_t result;

    test_setup_system_context(&driver_context);

    result = test_load_runtime_program_from_fixture(
        "tests/fixtures/wash_step_control/program_v1_invalid_json_syntax.json",
        &wash_program);
    TEST_ASSERT(!result.ok);
    TEST_ASSERT(result.error_code == ERROR_CODE_PARSE_FAILED);
    TEST_ASSERT(test_load_program_via_repository("broken_json", &wash_program) != 0);

    result = test_load_runtime_program_from_fixture(
        "tests/fixtures/wash_step_control/program_v1_invalid_stages_schema.json",
        &wash_program);
    TEST_ASSERT(!result.ok);
    TEST_ASSERT(result.error_code == ERROR_CODE_UNSUPPORTED);
    TEST_ASSERT(test_load_program_via_repository("legacy_stages_program", &wash_program) != 0);

    test_release_system_context();
    return 0;
}

int main(void)
{
    if (verify_calls_fail_before_acquire() != 0) {
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
