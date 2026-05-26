#ifndef TESTS_TEST_SUPPORT_H
#define TESTS_TEST_SUPPORT_H

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "adapters/config/json_program_parser.h"
#include "adapters/outbound/file_program_repository.h"
#include "application/coordinators/control_tick.h"
#include "application/coordinators/device_runtime.h"
#include "application/use_cases/process_formal_command.h"
#include "application/use_cases/query_wash_session_status.h"
#include "domain/model/wash_trigger_event.h"
#include "platform/drivers/simulated_brush_driver.h"
#include "platform/drivers/simulated_chemical_driver.h"
#include "platform/drivers/simulated_driver_context.h"
#include "platform/drivers/simulated_dryer_driver.h"
#include "platform/drivers/simulated_gantry_driver.h"
#include "platform/drivers/simulated_ro_water_driver.h"
#include "platform/drivers/simulated_sensor_driver.h"
#include "platform/linux/scheduler_linux.h"
#include "startup/app_bootstrap.h"
#include "domain/services/program_snapshot_service.h"
#include "domain/services/wash_session_state_machine.h"
#include "src/application/coordinators/device_runtime_layout.h"
#include "src/application/coordinators/device_runtime_private.h"
#include "src/startup/app_bootstrap_private.h"
#include "shared/error_codes.h"

#define TEST_ASSERT(expr) \
    do { \
        if (!(expr)) { \
            fprintf(stderr, "ASSERT FAILED: %s (%s:%d)\n", #expr, __FILE__, __LINE__); \
            return 1; \
        } \
    } while (0)

typedef struct test_runtime_binding_t
{
    bool occupied;
    app_t *app;
    device_runtime_t device_runtime;
    sensor_port_t sensor_port;
    actuator_port_t actuator_port;
} test_runtime_binding_t;

static test_runtime_binding_t g_test_runtime_binding;

operation_result_t test_runtime_friend_read_device_runtime(app_t *app, device_runtime_t *device_runtime);
scheduler_t *test_runtime_friend_scheduler(app_t *app);

static inline operation_result_t test_create_runtime(app_t **app,
                                                     simulated_driver_context_t *driver_context,
                                                     unsigned long control_period_ms);
static inline operation_result_t test_create_runtime_with_overrides(app_t **app,
                                                                    simulated_driver_context_t *driver_context,
                                                                    const scheduler_config_t *scheduler_config,
                                                                    FILE *command_input,
                                                                    FILE *command_output,
                                                                    FILE *command_error,
                                                                    const char *config_root);

static inline void test_purge_stale_runtime_bindings(void)
{
    if (g_test_runtime_binding.occupied
        && !device_runtime_private_require_active(g_test_runtime_binding.device_runtime).ok)
    {
        memset(&g_test_runtime_binding, 0, sizeof(g_test_runtime_binding));
    }
}

static inline test_runtime_binding_t *test_find_runtime_binding(device_runtime_t device_runtime)
{
    test_purge_stale_runtime_bindings();
    if (g_test_runtime_binding.occupied && g_test_runtime_binding.device_runtime == device_runtime)
    {
        return &g_test_runtime_binding;
    }
    return 0;
}

static inline test_runtime_binding_t *test_allocate_runtime_binding(void)
{
    test_purge_stale_runtime_bindings();
    if (!g_test_runtime_binding.occupied)
    {
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

static inline void test_setup_device_runtime(device_runtime_t *device_runtime,
                                             simulated_driver_context_t *driver_context)
{
    app_t *app;
    operation_result_t result;

    if (device_runtime == 0)
    {
        abort();
    }
    result = test_create_runtime(&app, driver_context, 100ul);
    if (!result.ok || app == 0)
    {
        fprintf(stderr, "FAILED TO CREATE app\n");
        abort();
    }
    result = test_runtime_friend_read_device_runtime(app, device_runtime);
    if (!result.ok || *device_runtime == 0)
    {
        fprintf(stderr, "FAILED TO READ app device_runtime\n");
        abort();
    }
}

/** @deprecated 使用 test_setup_device_runtime。 */
static inline void test_setup_system_context(device_runtime_t *device_runtime,
                                             simulated_driver_context_t *driver_context)
{
    test_setup_device_runtime(device_runtime, driver_context);
}

static inline void test_release_device_runtime(device_runtime_t device_runtime)
{
    test_runtime_binding_t *binding;
    operation_result_t result;

    binding = test_find_runtime_binding(device_runtime);
    if (binding != 0)
    {
        result = app_destroy(binding->app);
        if (!result.ok)
        {
            fprintf(stderr, "FAILED TO DESTROY app\n");
            abort();
        }
        memset(binding, 0, sizeof(*binding));
        return;
    }

    result = device_runtime_release(device_runtime);
    if (!result.ok)
    {
        fprintf(stderr, "FAILED TO RELEASE device_runtime\n");
        abort();
    }
}

/** @deprecated 使用 test_release_device_runtime。 */
static inline void test_release_system_context(device_runtime_t device_runtime)
{
    test_release_device_runtime(device_runtime);
}

static inline operation_result_t test_load_runtime_program_from_fixture(device_runtime_t device_runtime,
                                                                        const char *fixture_path,
                                                                        wash_program_t *wash_program)
{
    operation_result_t result;
    wash_program_t parsed_program;

    result = json_program_parser_parse(fixture_path, &parsed_program);
    if (!result.ok)
    {
        return result;
    }
    file_program_repository_set_runtime_program(device_runtime, &parsed_program, parsed_program.revision);
    if (wash_program != 0)
    {
        *wash_program = parsed_program;
    }
    return operation_result_ok();
}

static inline int test_load_program_via_repository(device_runtime_t device_runtime,
                                                   const char *program_id,
                                                   wash_program_t *wash_program)
{
    const program_repository_port_t *program_repository_port;

    if (program_id == 0 || wash_program == 0)
    {
        return -1;
    }

    program_repository_port = device_runtime_program_repository_port(device_runtime);
    if (program_repository_port == 0 || program_repository_port->context == 0
        || program_repository_port->load_program == 0)
    {
        return -1;
    }

    return program_repository_port->load_program(program_repository_port->context, program_id, wash_program);
}

static inline void test_assign_trigger_identity(device_runtime_t device_runtime,
                                                wash_trigger_event_t *wash_trigger_event)
{
    if (wash_trigger_event == 0)
    {
        return;
    }

    snprintf(wash_trigger_event->trigger_id, sizeof(wash_trigger_event->trigger_id), "test-%lu-%u",
             device_runtime_current_time_ms(device_runtime), device_runtime_pending_trigger_count(device_runtime));
    strncpy(wash_trigger_event->source, "test-helper", sizeof(wash_trigger_event->source) - 1);
}

static inline unsigned int has_pending_trigger_count(device_runtime_t device_runtime, const char *trigger_id)
{
    return device_runtime_count_pending_triggers_by_id(device_runtime, trigger_id);
}

static inline unsigned int test_count_pending_trigger_type(const device_runtime_t device_runtime,
                                                           trigger_type_t trigger_type)
{
    return device_runtime_count_pending_triggers_by_type(device_runtime, trigger_type);
}

static inline operation_result_t test_submit_trigger_and_drain(device_runtime_t device_runtime,
                                                              wash_trigger_event_t *wash_trigger_event)
{
    operation_result_t result;

    if (wash_trigger_event->trigger_id[0] == '\0')
    {
        test_assign_trigger_identity(device_runtime, wash_trigger_event);
    }
    result = control_tick_submit_trigger(device_runtime, wash_trigger_event);
    if (!result.ok)
    {
        return result;
    }
    while (has_pending_trigger_count(device_runtime, wash_trigger_event->trigger_id) > 0u)
    {
        result = control_tick_run(device_runtime);
        if (!result.ok && has_pending_trigger_count(device_runtime, wash_trigger_event->trigger_id) > 0u)
        {
            return result;
        }
    }
    return result;
}

static inline operation_result_t test_flush_pending_runtime(device_runtime_t device_runtime)
{
    operation_result_t result;
    unsigned int spin_guard;

    result = operation_result_ok();
    spin_guard = 0u;
    while (device_runtime_has_pending_work(device_runtime) && spin_guard < 64u)
    {
        result = control_tick_run(device_runtime);
        if (!result.ok)
        {
            return result;
        }
        spin_guard += 1u;
    }
    if (device_runtime_has_pending_work(device_runtime))
    {
        return operation_result_fail(ERROR_CODE_TIMEOUT);
    }
    return result;
}

static inline void test_rebuild_formal_response_line(device_runtime_t device_runtime, char *response_line,
                                                     size_t response_line_size)
{
    const char *detail;
    const char *result_code;

    if (response_line == 0 || response_line_size == 0u)
    {
        return;
    }

    result_code = device_runtime_last_result_code(device_runtime)[0] != '\0'
                      ? device_runtime_last_result_code(device_runtime)
                      : "accepted";
    detail = device_runtime_last_reason_code(device_runtime)[0] != '\0' ? device_runtime_last_reason_code(device_runtime)
                                                                      : "none";
    process_formal_command_format_response(response_line, response_line_size, result_code,
                                           process_formal_command_result_is_accepted(result_code), detail);
}

static inline operation_result_t test_process_command(device_runtime_t device_runtime, const char *command_line,
                                                    char *response_line, size_t response_line_size)
{
    return process_formal_command_execute(device_runtime, command_line, response_line, response_line_size);
}

static inline operation_result_t test_process_command_and_flush(device_runtime_t device_runtime,
                                                                const char *command_line, char *response_line,
                                                                size_t response_line_size)
{
    operation_result_t result;
    unsigned int pending_before;

    pending_before = device_runtime_pending_trigger_count(device_runtime);
    result = test_process_command(device_runtime, command_line, response_line, response_line_size);
    if (!result.ok)
    {
        return result;
    }
    if (device_runtime_pending_trigger_count(device_runtime) > pending_before)
    {
        result = test_flush_pending_runtime(device_runtime);
        if (!result.ok)
        {
            if (device_runtime_pending_trigger_count(device_runtime) <= pending_before)
            {
                test_rebuild_formal_response_line(device_runtime, response_line, response_line_size);
            }
            return result;
        }
        test_rebuild_formal_response_line(device_runtime, response_line, response_line_size);
    }
    return result;
}

static inline operation_result_t test_clear_fault(device_runtime_t device_runtime)
{
    char response_line[512];

    return test_process_command(device_runtime, "fault clear", response_line, sizeof(response_line));
}

static inline operation_result_t test_clear_fault_and_flush(device_runtime_t device_runtime)
{
    char response_line[512];

    return test_process_command_and_flush(device_runtime, "fault clear", response_line, sizeof(response_line));
}

static inline operation_result_t test_homing_system(device_runtime_t device_runtime)
{
    char response_line[512];

    return test_process_command(device_runtime, "homing", response_line, sizeof(response_line));
}

static inline operation_result_t test_homing_system_and_flush(device_runtime_t device_runtime)
{
    char response_line[512];

    return test_process_command_and_flush(device_runtime, "homing", response_line, sizeof(response_line));
}

static inline operation_result_t test_ensure_idle_device_state(device_runtime_t device_runtime)
{
    device_state_t device_state;
    operation_result_t result;

    device_state = device_runtime_private_device_state(device_runtime);
    if (device_state == DEVICE_STATE_IDLE)
    {
        return operation_result_ok();
    }
    if (device_state == DEVICE_STATE_EXCEPTION)
    {
        result = test_clear_fault_and_flush(device_runtime);
        if (!result.ok)
        {
            return result;
        }
        device_state = device_runtime_private_device_state(device_runtime);
    }
    if (device_state != DEVICE_STATE_STOPPED)
    {
        return operation_result_fail(ERROR_CODE_INVALID_STATE);
    }
    return test_homing_system_and_flush(device_runtime);
}

static inline operation_result_t test_start_session(device_runtime_t device_runtime, const char *program_id)
{
    char command_line[256];
    char response_line[512];
    operation_result_t result;

    result = test_ensure_idle_device_state(device_runtime);
    if (!result.ok)
    {
        return result;
    }
    snprintf(command_line, sizeof(command_line), "start %s", program_id != 0 ? program_id : "");
    return test_process_command(device_runtime, command_line, response_line, sizeof(response_line));
}

static inline operation_result_t test_start_session_and_flush(device_runtime_t device_runtime, const char *program_id)
{
    char command_line[256];
    char response_line[512];
    operation_result_t result;

    result = test_ensure_idle_device_state(device_runtime);
    if (!result.ok)
    {
        return result;
    }
    snprintf(command_line, sizeof(command_line), "start %s", program_id != 0 ? program_id : "");
    return test_process_command_and_flush(device_runtime, command_line, response_line, sizeof(response_line));
}

static inline operation_result_t test_submit_fault_with_reason(device_runtime_t device_runtime, const char *fault_code,
                                                               const char *fault_reason)
{
    wash_trigger_event_t wash_trigger_event;

    wash_trigger_event_init(&wash_trigger_event, TRIGGER_TYPE_FAULT, 0, fault_code,
                            fault_reason != 0 ? fault_reason : "test-fault",
                            device_runtime_current_time_ms(device_runtime));
    return test_submit_trigger_and_drain(device_runtime, &wash_trigger_event);
}

static inline operation_result_t test_submit_fault(device_runtime_t device_runtime, const char *fault_code)
{
    return test_submit_fault_with_reason(device_runtime, fault_code, "test-fault");
}

static inline operation_result_t test_submit_fault_clear(device_runtime_t device_runtime)
{
    wash_trigger_event_t wash_trigger_event;

    wash_trigger_event_init(&wash_trigger_event, TRIGGER_TYPE_FAULT, 0, "clear", 0,
                            device_runtime_current_time_ms(device_runtime));
    return test_submit_trigger_and_drain(device_runtime, &wash_trigger_event);
}

static inline operation_result_t test_submit_stop(device_runtime_t device_runtime, const char *reason_code)
{
    wash_trigger_event_t wash_trigger_event;

    wash_trigger_event_init(&wash_trigger_event, TRIGGER_TYPE_STOP, 0, reason_code, "stop-command",
                            device_runtime_current_time_ms(device_runtime));
    return test_submit_trigger_and_drain(device_runtime, &wash_trigger_event);
}

static inline operation_result_t test_tick(device_runtime_t device_runtime, unsigned long elapsed_ms)
{
    control_tick_advance_time(device_runtime, elapsed_ms);
    return control_tick_run(device_runtime);
}

static inline operation_result_t test_fire_timeout(device_runtime_t device_runtime, unsigned long elapsed_ms)
{
    return test_tick(device_runtime, elapsed_ms);
}

static inline scheduler_t *test_create_scheduler(device_runtime_t device_runtime, unsigned long control_period_ms)
{
    test_runtime_binding_t *binding;
    app_status_view_t status_view;
    scheduler_t *scheduler;
    operation_result_t result;

    binding = test_find_runtime_binding(device_runtime);
    if (binding == 0)
    {
        fprintf(stderr, "FAILED TO FIND runtime binding for device_runtime\n");
        abort();
    }

    result = app_read_state(binding->app, &status_view);
    if (!result.ok || !status_view.scheduler_view_available)
    {
        fprintf(stderr, "FAILED TO READ app state\n");
        abort();
    }
    if (status_view.scheduler_view.control_period_ms != control_period_ms)
    {
        fprintf(stderr, "UNEXPECTED scheduler control period\n");
        abort();
    }

    scheduler = test_runtime_friend_scheduler(binding->app);
    if (scheduler == 0)
    {
        fprintf(stderr, "FAILED TO READ app scheduler\n");
        abort();
    }
    return scheduler;
}

static inline operation_result_t test_scheduler_read_bound_view(device_runtime_t device_runtime,
                                                                scheduler_state_view_t *state_view)
{
    scheduler_t *scheduler;

    if (state_view == 0)
    {
        return operation_result_fail(ERROR_CODE_INVALID_ARGUMENT);
    }
    scheduler = (scheduler_t *)device_runtime_bound_scheduler(device_runtime);
    if (scheduler == 0)
    {
        return operation_result_fail(ERROR_CODE_INVALID_STATE);
    }
    return scheduler_read_view(scheduler, state_view);
}

static inline scheduler_t *test_scheduler_create_unbound(device_runtime_t device_runtime,
                                                         const scheduler_config_t *scheduler_config,
                                                         const scheduler_stdio_t *scheduler_stdio)
{
    scheduler_runtime_port_t scheduler_port;

    if (device_runtime == 0)
    {
        return 0;
    }
    scheduler_runtime_port_init_from_device_runtime(&scheduler_port, device_runtime);
    return scheduler_create(&scheduler_port, scheduler_config, scheduler_stdio);
}

static inline void test_init_scheduler_config(scheduler_config_t *scheduler_config, unsigned long control_period_ms)
{
    if (scheduler_config == 0)
    {
        abort();
    }

    memset(scheduler_config, 0, sizeof(*scheduler_config));
    scheduler_config->control_period_ms = control_period_ms;
    scheduler_config->command_event_source_enabled = true;
    scheduler_config->notification_event_source_enabled = true;
    scheduler_config->exit_event_source_enabled = true;
    scheduler_config->exit_mode = SCHEDULER_EXIT_MODE_BOUNDED_DRAIN;
    scheduler_config->bounded_drain_ticks = 4u;
    scheduler_config->max_triggers_per_tick = 1u;
    scheduler_config->overrun_warning_threshold_ms = control_period_ms;
}

static inline operation_result_t test_create_runtime(app_t **app, simulated_driver_context_t *driver_context,
                                                     unsigned long control_period_ms)
{
    scheduler_config_t scheduler_config;

    test_init_scheduler_config(&scheduler_config, control_period_ms);
    return test_create_runtime_with_overrides(app, driver_context, &scheduler_config, 0, 0, 0, "./configs");
}

static inline operation_result_t test_create_runtime_with_overrides(app_t **app,
                                                                  simulated_driver_context_t *driver_context,
                                                                  const scheduler_config_t *scheduler_config,
                                                                  FILE *command_input, FILE *command_output,
                                                                  FILE *command_error, const char *config_root)
{
    app_config_t app_config;
    test_runtime_binding_t *binding;
    operation_result_t result;

    if (app == 0 || driver_context == 0)
    {
        return operation_result_fail(ERROR_CODE_INVALID_ARGUMENT);
    }

    binding = test_allocate_runtime_binding();
    if (binding == 0)
    {
        return operation_result_fail(ERROR_CODE_RESOURCE_UNAVAILABLE);
    }

    test_bind_simulated_ports(driver_context, &binding->sensor_port, &binding->actuator_port);

    app_config_init(&app_config);
    app_config.sensor_port = &binding->sensor_port;
    app_config.actuator_port = &binding->actuator_port;
    app_config.scheduler_config = scheduler_config;
    app_config.command_input = command_input;
    app_config.command_output = command_output;
    app_config.command_error = command_error;
    app_config.config_root = config_root;
    result = app_create(app, &app_config);
    if (!result.ok || *app == 0)
    {
        memset(binding, 0, sizeof(*binding));
        return result;
    }

    result = test_runtime_friend_read_device_runtime(*app, &binding->device_runtime);
    if (!result.ok || binding->device_runtime == 0)
    {
        (void)app_destroy(*app);
        *app = 0;
        memset(binding, 0, sizeof(*binding));
        return result.ok ? operation_result_fail(ERROR_CODE_INVALID_STATE) : result;
    }

    binding->occupied = true;
    binding->app = *app;
    return operation_result_ok();
}

static inline program_snapshot_service_args_t test_build_program_snapshot_service_args(
    device_runtime_t device_runtime)
{
    program_snapshot_service_args_t program_snapshot_service_args;

    device_runtime_private_build_program_snapshot_service_args(device_runtime,
                                                                &program_snapshot_service_args);
    return program_snapshot_service_args;
}

static inline wash_session_service_args_t test_build_wash_session_service_args(device_runtime_t device_runtime)
{
    wash_session_service_args_t wash_session_service_args;

    device_runtime_private_build_session_service_args(device_runtime, &wash_session_service_args);
    return wash_session_service_args;
}

static inline operation_result_t test_runtime_device_runtime(app_t *app, device_runtime_t *device_runtime)
{
    return test_runtime_friend_read_device_runtime(app, device_runtime);
}

/** @deprecated 使用 test_runtime_device_runtime。 */
static inline operation_result_t test_runtime_system_context(app_t *app, device_runtime_t *device_runtime)
{
    return test_runtime_device_runtime(app, device_runtime);
}

static inline scheduler_t *test_runtime_scheduler(app_t *app)
{
    return test_runtime_friend_scheduler(app);
}

static inline int test_scheduler_tick(scheduler_t *scheduler, unsigned int expiration_count)
{
    operation_result_t result;

    result = scheduler_linux_test_inject_period(scheduler, expiration_count);
    TEST_ASSERT(result.ok);
    return 0;
}

static inline int test_scheduler_command(scheduler_t *scheduler, const char *command_line, char *response_line,
                                         size_t response_line_size)
{
    operation_result_t result;

    result = scheduler_linux_test_inject_command(scheduler, command_line, response_line, response_line_size);
    TEST_ASSERT(result.ok);
    return 0;
}

static inline int test_scheduler_homing(scheduler_t *scheduler)
{
    char response_line[512];

    return test_scheduler_command(scheduler, "homing", response_line, sizeof(response_line));
}

static inline int test_scheduler_notification(scheduler_t *scheduler, unsigned int notification_count)
{
    operation_result_t result;

    result = scheduler_linux_test_inject_notification(scheduler, notification_count);
    TEST_ASSERT(result.ok);
    return 0;
}

static inline int test_scheduler_exit(scheduler_t *scheduler, bool immediate)
{
    operation_result_t result;

    result = scheduler_linux_test_inject_exit(scheduler, immediate);
    TEST_ASSERT(result.ok);
    return 0;
}

static inline const char *test_latest_result_code(const device_runtime_t device_runtime)
{
    return device_runtime_last_result_code(device_runtime)[0] != '\0' ? device_runtime_last_result_code(device_runtime)
                                                                    : "none";
}

static inline const char *test_latest_reason_code(const device_runtime_t device_runtime)
{
    return device_runtime_last_reason_code(device_runtime)[0] != '\0' ? device_runtime_last_reason_code(device_runtime)
                                                                    : "none";
}

#endif
