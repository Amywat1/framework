#ifndef TESTS_TEST_SUPPORT_H
#define TESTS_TEST_SUPPORT_H

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "adapters/config/json_program_parser.h"
#include "adapters/inbound/cli_command_adapter.h"
#include "adapters/logging/file_event_logger.h"
#include "adapters/outbound/file_program_repository.h"
#include "application/coordinators/controller_runtime.h"
#include "application/coordinators/system_context.h"
#include "application/use_cases/query_wash_session_status.h"
#include "domain/model/wash_trigger_event.h"
#include "platform/drivers/simulated_brush_driver.h"
#include "platform/drivers/simulated_chemical_driver.h"
#include "platform/drivers/simulated_driver_context.h"
#include "platform/drivers/simulated_dryer_driver.h"
#include "platform/drivers/simulated_gantry_driver.h"
#include "platform/drivers/simulated_ro_water_driver.h"
#include "platform/drivers/simulated_sensor_driver.h"
#include "platform/linux/controller_scheduler_linux.h"
#include "platform/linux/main_loop.h"
#include "src/application/coordinators/system_context_runtime_layout.h"
#include "shared/error_codes.h"

#define TEST_ASSERT(expr) \
    do { \
        if (!(expr)) { \
            fprintf(stderr, "ASSERT FAILED: %s (%s:%d)\n", #expr, __FILE__, __LINE__); \
            return 1; \
        } \
    } while (0)

typedef struct test_runtime_binding_t {
    bool occupied;
    controller_runtime_t *controller_runtime;
    system_context_t system_context;
    sensor_port_t sensor_port;
    actuator_port_t actuator_port;
} test_runtime_binding_t;

static test_runtime_binding_t g_test_runtime_binding;

operation_result_t test_runtime_friend_read_system_context(controller_runtime_t *controller_runtime,
    system_context_t *system_context);
controller_scheduler_t *test_runtime_friend_scheduler(controller_runtime_t *controller_runtime);

static inline operation_result_t test_create_runtime(controller_runtime_t **controller_runtime,
    simulated_driver_context_t *driver_context,
    unsigned long control_period_ms);
static inline operation_result_t test_create_runtime_with_overrides(controller_runtime_t **controller_runtime,
    simulated_driver_context_t *driver_context,
    const controller_scheduler_config_t *controller_scheduler_config,
    FILE *command_input,
    FILE *command_output,
    FILE *command_error,
    const char *config_root,
    const char *event_log_path);

static inline void test_purge_stale_runtime_bindings(void)
{
    if (g_test_runtime_binding.occupied
        && !system_context_private_require_active(g_test_runtime_binding.system_context).ok) {
        memset(&g_test_runtime_binding, 0, sizeof(g_test_runtime_binding));
    }
}

static inline test_runtime_binding_t *test_find_runtime_binding(system_context_t system_context)
{
    test_purge_stale_runtime_bindings();
    if (g_test_runtime_binding.occupied && g_test_runtime_binding.system_context == system_context) {
        return &g_test_runtime_binding;
    }
    return 0;
}

static inline test_runtime_binding_t *test_allocate_runtime_binding(void)
{
    test_purge_stale_runtime_bindings();
    if (!g_test_runtime_binding.occupied) {
        memset(&g_test_runtime_binding, 0, sizeof(g_test_runtime_binding));
        return &g_test_runtime_binding;
    }
    return 0;
}

static inline void test_bind_simulated_ports(simulated_driver_context_t *driver_context,
    sensor_port_t *sensor_port,
    actuator_port_t *actuator_port)
{
    simulated_driver_context_init(driver_context);
    memset(sensor_port, 0, sizeof(*sensor_port));
    memset(actuator_port, 0, sizeof(*actuator_port));
    simulated_sensor_driver_bind(sensor_port, driver_context);
    simulated_gantry_driver_bind(actuator_port, driver_context);
    simulated_brush_driver_bind(actuator_port, driver_context);
    simulated_chemical_driver_bind(actuator_port, driver_context);
    simulated_ro_water_driver_bind(actuator_port, driver_context);
    simulated_dryer_driver_bind(actuator_port, driver_context);
}

static inline void test_setup_system_context(system_context_t *system_context, simulated_driver_context_t *driver_context)
{
    controller_runtime_t *controller_runtime;
    operation_result_t result;

    if (system_context == 0) {
        abort();
    }
    result = test_create_runtime(&controller_runtime, driver_context, 100ul);
    if (!result.ok || controller_runtime == 0) {
        fprintf(stderr, "FAILED TO CREATE controller_runtime\n");
        abort();
    }
    result = test_runtime_friend_read_system_context(controller_runtime, system_context);
    if (!result.ok || *system_context == 0) {
        fprintf(stderr, "FAILED TO READ controller_runtime system_context\n");
        abort();
    }
}

static inline void test_release_system_context(system_context_t system_context)
{
    test_runtime_binding_t *binding;
    operation_result_t result;

    binding = test_find_runtime_binding(system_context);
    if (binding != 0) {
        result = controller_runtime_destroy(binding->controller_runtime);
        if (!result.ok) {
            fprintf(stderr, "FAILED TO DESTROY controller_runtime\n");
            abort();
        }
        memset(binding, 0, sizeof(*binding));
        return;
    }

    result = system_context_release(system_context);
    if (!result.ok) {
        fprintf(stderr, "FAILED TO RELEASE system_context\n");
        abort();
    }
}

static inline operation_result_t test_load_runtime_program_from_fixture(system_context_t system_context,
    const char *fixture_path,
    wash_program_t *wash_program)
{
    operation_result_t result;
    wash_program_t parsed_program;

    result = json_program_parser_parse(fixture_path, &parsed_program);
    if (!result.ok) {
        return result;
    }
    file_program_repository_set_runtime_program(system_context, &parsed_program, parsed_program.revision);
    if (wash_program != 0) {
        *wash_program = parsed_program;
    }
    return operation_result_ok();
}

static inline int test_load_program_via_repository(system_context_t system_context,
    const char *program_id,
    wash_program_t *wash_program)
{
    const program_repository_port_t *program_repository_port;

    if (program_id == 0 || wash_program == 0) {
        return -1;
    }

    program_repository_port = system_context_program_repository_port(system_context);
    if (program_repository_port == 0
        || program_repository_port->context == 0
        || program_repository_port->load_program == 0) {
        return -1;
    }

    return program_repository_port->load_program(program_repository_port->context, program_id, wash_program);
}

static inline void test_assign_trigger_identity(system_context_t system_context, wash_trigger_event_t *wash_trigger_event)
{
    if (wash_trigger_event == 0) {
        return;
    }

    snprintf(wash_trigger_event->trigger_id,
        sizeof(wash_trigger_event->trigger_id),
        "test-%lu-%u",
        system_context_current_time_ms(system_context),
        system_context_pending_trigger_count(system_context));
    strncpy(wash_trigger_event->source, "test-helper", sizeof(wash_trigger_event->source) - 1);
}

static inline unsigned int has_pending_trigger_count(system_context_t system_context, const char *trigger_id)
{
    return system_context_count_pending_triggers_by_id(system_context, trigger_id);
}

static inline unsigned int test_count_pending_trigger_type(const system_context_t system_context, trigger_type_t trigger_type)
{
    return system_context_count_pending_triggers_by_type(system_context, trigger_type);
}

static inline operation_result_t test_submit_trigger_and_drain(system_context_t system_context, wash_trigger_event_t *wash_trigger_event)
{
    operation_result_t result;

    if (wash_trigger_event->trigger_id[0] == '\0') {
        test_assign_trigger_identity(system_context, wash_trigger_event);
    }
    result = main_loop_submit_trigger(system_context, wash_trigger_event);
    if (!result.ok) {
        return result;
    }
    while (has_pending_trigger_count(system_context, wash_trigger_event->trigger_id) > 0u) {
        result = main_loop_run(system_context);
        if (!result.ok && has_pending_trigger_count(system_context, wash_trigger_event->trigger_id) > 0u) {
            return result;
        }
    }
    return result;
}

static inline operation_result_t test_flush_pending_runtime(system_context_t system_context)
{
    operation_result_t result;
    unsigned int spin_guard;

    result = operation_result_ok();
    spin_guard = 0u;
    while (system_context_has_pending_runtime_work(system_context) && spin_guard < 64u) {
        result = main_loop_run(system_context);
        if (!result.ok) {
            return result;
        }
        spin_guard += 1u;
    }
    if (system_context_has_pending_runtime_work(system_context)) {
        return operation_result_fail(ERROR_CODE_TIMEOUT);
    }
    return result;
}

static inline void test_rebuild_formal_response_line(system_context_t system_context,
    char *response_line,
    size_t response_line_size)
{
    const char *detail;
    const char *result_code;
    bool accepted;

    if (response_line == 0 || response_line_size == 0u) {
        return;
    }

    result_code = system_context_last_result_code(system_context)[0] != '\0'
        ? system_context_last_result_code(system_context)
        : "accepted";
    detail = system_context_last_reason_code(system_context)[0] != '\0'
        ? system_context_last_reason_code(system_context)
        : "none";
    accepted = strcmp(result_code, "ignored") != 0
        && strcmp(result_code, "rejected") != 0
        && strcmp(result_code, "error") != 0;
    snprintf(response_line,
        response_line_size,
        "result=%s accepted=%s detail=%s",
        result_code,
        accepted ? "true" : "false",
        detail);
}

static inline operation_result_t test_process_command(system_context_t system_context,
    const char *command_line,
    char *response_line,
    size_t response_line_size)
{
    return cli_command_adapter_execute_formal_line(system_context, command_line, response_line, response_line_size);
}

static inline operation_result_t test_process_command_and_flush(system_context_t system_context,
    const char *command_line,
    char *response_line,
    size_t response_line_size)
{
    operation_result_t result;
    unsigned int pending_before;

    pending_before = system_context_pending_trigger_count(system_context);
    result = test_process_command(system_context, command_line, response_line, response_line_size);
    if (!result.ok) {
        return result;
    }
    if (system_context_pending_trigger_count(system_context) > pending_before) {
        result = test_flush_pending_runtime(system_context);
        if (!result.ok) {
            if (system_context_pending_trigger_count(system_context) <= pending_before) {
                test_rebuild_formal_response_line(system_context, response_line, response_line_size);
            }
            return result;
        }
        test_rebuild_formal_response_line(system_context, response_line, response_line_size);
    }
    return result;
}

static inline operation_result_t test_clear_fault(system_context_t system_context)
{
    char response_line[512];

    return test_process_command(system_context, "fault clear", response_line, sizeof(response_line));
}

static inline operation_result_t test_clear_fault_and_flush(system_context_t system_context)
{
    char response_line[512];

    return test_process_command_and_flush(system_context, "fault clear", response_line, sizeof(response_line));
}

static inline operation_result_t test_start_session(system_context_t system_context, const char *program_id)
{
    char command_line[256];
    char response_line[512];

    snprintf(command_line, sizeof(command_line), "start %s", program_id != 0 ? program_id : "");
    return test_process_command(system_context, command_line, response_line, sizeof(response_line));
}

static inline operation_result_t test_start_session_and_flush(system_context_t system_context, const char *program_id)
{
    char command_line[256];
    char response_line[512];

    snprintf(command_line, sizeof(command_line), "start %s", program_id != 0 ? program_id : "");
    return test_process_command_and_flush(system_context, command_line, response_line, sizeof(response_line));
}

static inline operation_result_t test_submit_fault_with_reason(system_context_t system_context,
    const char *fault_code,
    const char *fault_reason)
{
    wash_trigger_event_t wash_trigger_event;

    wash_trigger_event_init(&wash_trigger_event,
        TRIGGER_TYPE_FAULT,
        0,
        fault_code,
        fault_reason != 0 ? fault_reason : "test-fault",
        system_context_current_time_ms(system_context));
    return test_submit_trigger_and_drain(system_context, &wash_trigger_event);
}

static inline operation_result_t test_submit_stop(system_context_t system_context, const char *reason_code)
{
    wash_trigger_event_t wash_trigger_event;

    wash_trigger_event_init(&wash_trigger_event,
        TRIGGER_TYPE_STOP,
        0,
        reason_code,
        "stop-command",
        system_context_current_time_ms(system_context));
    return test_submit_trigger_and_drain(system_context, &wash_trigger_event);
}

static inline operation_result_t test_tick(system_context_t system_context, unsigned long elapsed_ms)
{
    main_loop_advance_time(system_context, elapsed_ms);
    return main_loop_run(system_context);
}

static inline controller_scheduler_t *test_create_scheduler(system_context_t system_context,
    unsigned long control_period_ms)
{
    test_runtime_binding_t *binding;
    controller_runtime_status_view_t status_view;
    controller_scheduler_t *controller_scheduler;
    operation_result_t result;

    binding = test_find_runtime_binding(system_context);
    if (binding == 0) {
        fprintf(stderr, "FAILED TO FIND runtime binding for system_context\n");
        abort();
    }

    result = controller_runtime_read_state(binding->controller_runtime, &status_view);
    if (!result.ok || !status_view.scheduler_view_available) {
        fprintf(stderr, "FAILED TO READ controller_runtime state\n");
        abort();
    }
    if (status_view.scheduler_view.control_period_ms != control_period_ms) {
        fprintf(stderr, "UNEXPECTED scheduler control period\n");
        abort();
    }

    controller_scheduler = test_runtime_friend_scheduler(binding->controller_runtime);
    if (controller_scheduler == 0) {
        fprintf(stderr, "FAILED TO READ controller_runtime scheduler\n");
        abort();
    }
    return controller_scheduler;
}

static inline void test_init_scheduler_config(controller_scheduler_config_t *controller_scheduler_config,
    unsigned long control_period_ms)
{
    if (controller_scheduler_config == 0) {
        abort();
    }

    memset(controller_scheduler_config, 0, sizeof(*controller_scheduler_config));
    controller_scheduler_config->control_period_ms = control_period_ms;
    controller_scheduler_config->command_event_source_enabled = true;
    controller_scheduler_config->notification_event_source_enabled = true;
    controller_scheduler_config->exit_event_source_enabled = true;
    controller_scheduler_config->exit_mode = CONTROLLER_SCHEDULER_EXIT_MODE_BOUNDED_DRAIN;
    controller_scheduler_config->bounded_drain_ticks = 4u;
    controller_scheduler_config->max_triggers_per_tick = 1u;
    controller_scheduler_config->overrun_warning_threshold_ms = control_period_ms;
    controller_scheduler_config->observability_enabled = true;
}

static inline operation_result_t test_create_runtime(controller_runtime_t **controller_runtime,
    simulated_driver_context_t *driver_context,
    unsigned long control_period_ms)
{
    controller_scheduler_config_t controller_scheduler_config;

    test_init_scheduler_config(&controller_scheduler_config, control_period_ms);
    return test_create_runtime_with_overrides(controller_runtime,
        driver_context,
        &controller_scheduler_config,
        0,
        0,
        0,
        "./configs",
        "./runtime/logs/test_events.log");
}

static inline operation_result_t test_create_runtime_with_overrides(controller_runtime_t **controller_runtime,
    simulated_driver_context_t *driver_context,
    const controller_scheduler_config_t *controller_scheduler_config,
    FILE *command_input,
    FILE *command_output,
    FILE *command_error,
    const char *config_root,
    const char *event_log_path)
{
    controller_runtime_config_t controller_runtime_config;
    test_runtime_binding_t *binding;
    operation_result_t result;

    if (controller_runtime == 0 || driver_context == 0) {
        return operation_result_fail(ERROR_CODE_INVALID_ARGUMENT);
    }

    binding = test_allocate_runtime_binding();
    if (binding == 0) {
        return operation_result_fail(ERROR_CODE_RESOURCE_UNAVAILABLE);
    }

    test_bind_simulated_ports(driver_context, &binding->sensor_port, &binding->actuator_port);

    controller_runtime_config_init(&controller_runtime_config);
    controller_runtime_config.sensor_port = &binding->sensor_port;
    controller_runtime_config.actuator_port = &binding->actuator_port;
    controller_runtime_config.scheduler_config = controller_scheduler_config;
    controller_runtime_config.command_input = command_input;
    controller_runtime_config.command_output = command_output;
    controller_runtime_config.command_error = command_error;
    controller_runtime_config.config_root = config_root;
    controller_runtime_config.event_log_path = event_log_path;
    result = controller_runtime_create(controller_runtime, &controller_runtime_config);
    if (!result.ok || *controller_runtime == 0) {
        memset(binding, 0, sizeof(*binding));
        return result;
    }

    result = test_runtime_friend_read_system_context(*controller_runtime, &binding->system_context);
    if (!result.ok || binding->system_context == 0) {
        (void)controller_runtime_destroy(*controller_runtime);
        *controller_runtime = 0;
        memset(binding, 0, sizeof(*binding));
        return result.ok ? operation_result_fail(ERROR_CODE_INVALID_STATE) : result;
    }

    binding->occupied = true;
    binding->controller_runtime = *controller_runtime;
    return operation_result_ok();
}

static inline operation_result_t test_runtime_system_context(controller_runtime_t *controller_runtime,
    system_context_t *system_context)
{
    return test_runtime_friend_read_system_context(controller_runtime, system_context);
}

static inline controller_scheduler_t *test_runtime_scheduler(controller_runtime_t *controller_runtime)
{
    return test_runtime_friend_scheduler(controller_runtime);
}

static inline int test_scheduler_tick(controller_scheduler_t *controller_scheduler, unsigned int expiration_count)
{
    operation_result_t result;

    result = controller_scheduler_linux_test_inject_period(controller_scheduler, expiration_count);
    TEST_ASSERT(result.ok);
    return 0;
}

static inline int test_scheduler_command(controller_scheduler_t *controller_scheduler,
    const char *command_line,
    char *response_line,
    size_t response_line_size)
{
    operation_result_t result;

    result = controller_scheduler_linux_test_inject_command(controller_scheduler,
        command_line,
        response_line,
        response_line_size);
    TEST_ASSERT(result.ok);
    return 0;
}

static inline int test_scheduler_notification(controller_scheduler_t *controller_scheduler, unsigned int notification_count)
{
    operation_result_t result;

    result = controller_scheduler_linux_test_inject_notification(controller_scheduler, notification_count);
    TEST_ASSERT(result.ok);
    return 0;
}

static inline int test_scheduler_exit(controller_scheduler_t *controller_scheduler, bool immediate)
{
    operation_result_t result;

    result = controller_scheduler_linux_test_inject_exit(controller_scheduler, immediate);
    TEST_ASSERT(result.ok);
    return 0;
}

static inline const char *test_latest_result_code(const system_context_t system_context)
{
    return system_context_last_result_code(system_context)[0] != '\0' ? system_context_last_result_code(system_context) : "none";
}

static inline const char *test_latest_reason_code(const system_context_t system_context)
{
    return system_context_last_reason_code(system_context)[0] != '\0' ? system_context_last_reason_code(system_context) : "none";
}

#endif
