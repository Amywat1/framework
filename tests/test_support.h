#ifndef TESTS_TEST_SUPPORT_H
#define TESTS_TEST_SUPPORT_H

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "adapters/config/json_program_parser.h"
#include "adapters/outbound/file_program_repository.h"
#include "application/coordinators/control_tick.h"
#include "application/coordinators/control_context.h"
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
#include "application/coordinators/scheduler_runtime_port.h"
#include "platform/linux/scheduler_linux.h"
#include "platform/scheduler.h"
#include "domain/services/program_snapshot_service.h"
#include "domain/services/wash_session_state_machine.h"
#include "src/application/coordinators/control_context_layout.h"
#include "src/application/coordinators/control_context_private.h"
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
    bool initialized;
    scheduler_t *scheduler;
    sensor_port_t sensor_port;
    actuator_port_t actuator_port;
} test_runtime_binding_t;

static test_runtime_binding_t g_test_runtime_binding;

static inline void test_init_scheduler_config(scheduler_config_t *scheduler_config, unsigned long control_period_ms);

static inline void test_purge_stale_runtime_bindings(void)
{
    if (g_test_runtime_binding.initialized
        && !control_context_require_active().ok)
    {
        memset(&g_test_runtime_binding, 0, sizeof(g_test_runtime_binding));
    }
}

static inline bool test_has_active_runtime_binding(void)
{
    test_purge_stale_runtime_bindings();
    return g_test_runtime_binding.initialized;
}

static inline test_runtime_binding_t *test_allocate_runtime_binding(void)
{
    test_purge_stale_runtime_bindings();
    if (!g_test_runtime_binding.initialized)
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

static inline operation_result_t test_bind_control_context_binding(test_runtime_binding_t *binding,
                                                                simulated_driver_context_t *driver_context,
                                                                const scheduler_config_t *scheduler_config,
                                                                const char *config_root)
{
    scheduler_runtime_port_t scheduler_port;
    scheduler_stdio_t scheduler_stdio;
    operation_result_t result;

    if (binding == 0 || driver_context == 0 || scheduler_config == 0 || config_root == 0)
    {
        return operation_result_fail(ERROR_CODE_INVALID_ARGUMENT);
    }

    test_bind_simulated_ports(driver_context, &binding->sensor_port, &binding->actuator_port);
    result = control_context_init();
    if (!result.ok)
    {
        return result;
    }

    control_context_set_sensor_port(&binding->sensor_port);
    control_context_set_actuator_port(&binding->actuator_port);
    result = file_program_repository_init(config_root);
    if (!result.ok)
    {
        (void)control_context_deinit();
        return result;
    }

    result = control_context_private_enter_stopped();
    if (!result.ok)
    {
        (void)control_context_deinit();
        return result;
    }

    memset(&scheduler_stdio, 0, sizeof(scheduler_stdio));
    scheduler_runtime_port_init_from_control_context(&scheduler_port);
    binding->scheduler = scheduler_create(&scheduler_port, scheduler_config, &scheduler_stdio);
    if (binding->scheduler == 0)
    {
        (void)control_context_deinit();
        return operation_result_fail(ERROR_CODE_IO_FAILED);
    }

    result = control_context_bind_scheduler(binding->scheduler);
    if (!result.ok)
    {
        scheduler_destroy(binding->scheduler);
        binding->scheduler = 0;
        (void)control_context_deinit();
        return result;
    }

    binding->initialized = true;
    return operation_result_ok();
}

static inline void test_setup_control_context(simulated_driver_context_t *driver_context)
{
    scheduler_config_t scheduler_config;
    test_runtime_binding_t *binding;
    operation_result_t result;

    binding = test_allocate_runtime_binding();
    if (binding == 0)
    {
        fprintf(stderr, "FAILED TO ALLOCATE runtime binding\n");
        abort();
    }

    test_init_scheduler_config(&scheduler_config, 100ul);
    result = test_bind_control_context_binding(binding, driver_context, &scheduler_config, "./configs");
    if (!result.ok)
    {
        fprintf(stderr, "FAILED TO CREATE control_context\n");
        abort();
    }
}

/** @deprecated 使用 test_setup_control_context。 */
static inline void test_setup_system_context(simulated_driver_context_t *driver_context)
{
    test_setup_control_context(driver_context);
}

static inline void test_release_control_context(void)
{
    test_runtime_binding_t *binding;
    operation_result_t result;

    binding = &g_test_runtime_binding;
    if (binding->initialized)
    {
        if (binding->scheduler != 0)
        {
            (void)control_context_unbind_scheduler();
            scheduler_destroy(binding->scheduler);
            binding->scheduler = 0;
        }
        result = control_context_deinit();
        if (!result.ok)
        {
            fprintf(stderr, "FAILED TO RELEASE control_context\n");
            abort();
        }
        memset(binding, 0, sizeof(*binding));
        return;
    }

    result = control_context_deinit();
    if (!result.ok)
    {
        fprintf(stderr, "FAILED TO RELEASE control_context\n");
        abort();
    }
}

/** @deprecated 使用 test_release_control_context。 */
static inline void test_release_system_context(void)
{
    test_release_control_context();
}

static inline operation_result_t test_load_runtime_program_from_fixture(const char *fixture_path,
                                                                        wash_program_t *wash_program)
{
    operation_result_t result;
    wash_program_t parsed_program;

    result = json_program_parser_parse(fixture_path, &parsed_program);
    if (!result.ok)
    {
        return result;
    }
    file_program_repository_set_runtime_program(&parsed_program, parsed_program.revision);
    if (wash_program != 0)
    {
        *wash_program = parsed_program;
    }
    return operation_result_ok();
}

static inline int test_load_program_via_repository(const char *program_id, wash_program_t *wash_program)
{
    const program_repository_port_t *program_repository_port;

    if (program_id == 0 || wash_program == 0)
    {
        return -1;
    }

    program_repository_port = control_context_program_repository_port();
    if (program_repository_port == 0 || program_repository_port->context == 0
        || program_repository_port->load_program == 0)
    {
        return -1;
    }

    return program_repository_port->load_program(program_repository_port->context, program_id, wash_program);
}

static inline void test_assign_trigger_identity(wash_trigger_event_t *wash_trigger_event)
{
    if (wash_trigger_event == 0)
    {
        return;
    }

    snprintf(wash_trigger_event->trigger_id, sizeof(wash_trigger_event->trigger_id), "test-%lu-%u",
             control_context_current_time_ms(), control_context_pending_trigger_count());
    strncpy(wash_trigger_event->source, "test-helper", sizeof(wash_trigger_event->source) - 1);
}

static inline unsigned int has_pending_trigger_count(const char *trigger_id)
{
    return control_context_count_pending_triggers_by_id(trigger_id);
}

static inline unsigned int test_count_pending_trigger_type(trigger_type_t trigger_type)
{
    return control_context_count_pending_triggers_by_type(trigger_type);
}

static inline operation_result_t test_submit_trigger_and_drain(wash_trigger_event_t *wash_trigger_event)
{
    operation_result_t result;

    if (wash_trigger_event->trigger_id[0] == '\0')
    {
        test_assign_trigger_identity(wash_trigger_event);
    }
    result = control_tick_submit_trigger(wash_trigger_event);
    if (!result.ok)
    {
        return result;
    }
    while (has_pending_trigger_count(wash_trigger_event->trigger_id) > 0u)
    {
        result = control_tick_run();
        if (!result.ok && has_pending_trigger_count(wash_trigger_event->trigger_id) > 0u)
        {
            return result;
        }
    }
    return result;
}

static inline operation_result_t test_flush_pending_runtime(void)
{
    operation_result_t result;
    unsigned int spin_guard;

    result = operation_result_ok();
    spin_guard = 0u;
    while (control_context_has_pending_work() && spin_guard < 64u)
    {
        result = control_tick_run();
        if (!result.ok)
        {
            return result;
        }
        spin_guard += 1u;
    }
    if (control_context_has_pending_work())
    {
        return operation_result_fail(ERROR_CODE_TIMEOUT);
    }
    return result;
}

static inline void test_rebuild_formal_response_line(char *response_line, size_t response_line_size)
{
    const char *detail;
    const char *result_code;

    if (response_line == 0 || response_line_size == 0u)
    {
        return;
    }

    result_code = control_context_last_result_code()[0] != '\0'
                      ? control_context_last_result_code()
                      : "accepted";
    detail = control_context_last_reason_code()[0] != '\0' ? control_context_last_reason_code()
                                                          : "none";
    process_formal_command_format_response(response_line, response_line_size, result_code,
                                           process_formal_command_result_is_accepted(result_code), detail);
}

static inline operation_result_t test_process_command(const char *command_line,
                                                    char *response_line, size_t response_line_size)
{
    return process_formal_command_execute(command_line, response_line, response_line_size);
}

static inline operation_result_t test_process_command_and_flush(const char *command_line, char *response_line,
                                                                size_t response_line_size)
{
    operation_result_t result;
    unsigned int pending_before;

    pending_before = control_context_pending_trigger_count();
    result = test_process_command(command_line, response_line, response_line_size);
    if (!result.ok)
    {
        return result;
    }
    if (control_context_pending_trigger_count() > pending_before)
    {
        result = test_flush_pending_runtime();
        if (!result.ok)
        {
            if (control_context_pending_trigger_count() <= pending_before)
            {
                test_rebuild_formal_response_line(response_line, response_line_size);
            }
            return result;
        }
        test_rebuild_formal_response_line(response_line, response_line_size);
    }
    return result;
}

static inline operation_result_t test_clear_fault(void)
{
    char response_line[512];

    return test_process_command("fault clear", response_line, sizeof(response_line));
}

static inline operation_result_t test_clear_fault_and_flush(void)
{
    char response_line[512];

    return test_process_command_and_flush("fault clear", response_line, sizeof(response_line));
}

static inline operation_result_t test_homing_system(void)
{
    char response_line[512];

    return test_process_command("homing", response_line, sizeof(response_line));
}

static inline operation_result_t test_homing_system_and_flush(void)
{
    char response_line[512];

    return test_process_command_and_flush("homing", response_line, sizeof(response_line));
}

static inline operation_result_t test_ensure_idle_device_state(void)
{
    device_state_t device_state;
    operation_result_t result;

    device_state = control_context_private_device_state();
    if (device_state == DEVICE_STATE_IDLE)
    {
        return operation_result_ok();
    }
    if (device_state == DEVICE_STATE_EXCEPTION)
    {
        result = test_clear_fault_and_flush();
        if (!result.ok)
        {
            return result;
        }
        device_state = control_context_private_device_state();
    }
    if (device_state != DEVICE_STATE_STOPPED)
    {
        return operation_result_fail(ERROR_CODE_INVALID_STATE);
    }
    return test_homing_system_and_flush();
}

static inline operation_result_t test_start_session(const char *program_id)
{
    char command_line[256];
    char response_line[512];
    operation_result_t result;

    result = test_ensure_idle_device_state();
    if (!result.ok)
    {
        return result;
    }
    snprintf(command_line, sizeof(command_line), "start %s", program_id != 0 ? program_id : "");
    return test_process_command(command_line, response_line, sizeof(response_line));
}

static inline operation_result_t test_start_session_and_flush(const char *program_id)
{
    char command_line[256];
    char response_line[512];
    operation_result_t result;

    result = test_ensure_idle_device_state();
    if (!result.ok)
    {
        return result;
    }
    snprintf(command_line, sizeof(command_line), "start %s", program_id != 0 ? program_id : "");
    return test_process_command_and_flush(command_line, response_line, sizeof(response_line));
}

static inline operation_result_t test_submit_fault_with_reason(const char *fault_code, const char *fault_reason)
{
    wash_trigger_event_t wash_trigger_event;

    wash_trigger_event_init(&wash_trigger_event, TRIGGER_TYPE_FAULT, 0, fault_code,
                            fault_reason != 0 ? fault_reason : "test-fault",
                            control_context_current_time_ms());
    return test_submit_trigger_and_drain(&wash_trigger_event);
}

static inline operation_result_t test_submit_fault(const char *fault_code)
{
    return test_submit_fault_with_reason(fault_code, "test-fault");
}

static inline operation_result_t test_submit_fault_clear(void)
{
    wash_trigger_event_t wash_trigger_event;

    wash_trigger_event_init(&wash_trigger_event, TRIGGER_TYPE_FAULT, 0, "clear", 0,
                            control_context_current_time_ms());
    return test_submit_trigger_and_drain(&wash_trigger_event);
}

static inline operation_result_t test_submit_stop(const char *reason_code)
{
    wash_trigger_event_t wash_trigger_event;

    wash_trigger_event_init(&wash_trigger_event, TRIGGER_TYPE_STOP, 0, reason_code, "stop-command",
                            control_context_current_time_ms());
    return test_submit_trigger_and_drain(&wash_trigger_event);
}

static inline operation_result_t test_tick(unsigned long elapsed_ms)
{
    control_tick_advance_time(elapsed_ms);
    return control_tick_run();
}

static inline operation_result_t test_fire_timeout(unsigned long elapsed_ms)
{
    return test_tick(elapsed_ms);
}

static inline scheduler_t *test_create_scheduler(unsigned long control_period_ms)
{
    scheduler_state_view_t status_view;
    operation_result_t result;

    if (!g_test_runtime_binding.initialized || g_test_runtime_binding.scheduler == 0)
    {
        fprintf(stderr, "FAILED TO FIND runtime binding\n");
        abort();
    }

    result = scheduler_read_view(g_test_runtime_binding.scheduler, &status_view);
    if (!result.ok)
    {
        fprintf(stderr, "FAILED TO READ scheduler state\n");
        abort();
    }
    if (status_view.control_period_ms != control_period_ms)
    {
        fprintf(stderr, "UNEXPECTED scheduler control period\n");
        abort();
    }
    return g_test_runtime_binding.scheduler;
}

static inline operation_result_t test_scheduler_read_bound_view(scheduler_state_view_t *state_view)
{
    scheduler_t *scheduler;

    if (state_view == 0)
    {
        return operation_result_fail(ERROR_CODE_INVALID_ARGUMENT);
    }
    scheduler = (scheduler_t *)control_context_bound_scheduler();
    if (scheduler == 0)
    {
        return operation_result_fail(ERROR_CODE_INVALID_STATE);
    }
    return scheduler_read_view(scheduler, state_view);
}

static inline scheduler_t *test_scheduler_create_unbound(const scheduler_config_t *scheduler_config,
                                                         const scheduler_stdio_t *scheduler_stdio)
{
    scheduler_runtime_port_t scheduler_port;

    scheduler_runtime_port_init_from_control_context(&scheduler_port);
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

static inline program_snapshot_service_args_t test_build_program_snapshot_service_args(void)
{
    program_snapshot_service_args_t program_snapshot_service_args;

    control_context_private_build_program_snapshot_service_args(&program_snapshot_service_args);
    return program_snapshot_service_args;
}

static inline wash_session_service_args_t test_build_wash_session_service_args(void)
{
    wash_session_service_args_t wash_session_service_args;

    control_context_private_build_session_service_args(&wash_session_service_args);
    return wash_session_service_args;
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

static inline const char *test_latest_result_code(void)
{
    return control_context_last_result_code()[0] != '\0' ? control_context_last_result_code()
                                                        : "none";
}

static inline const char *test_latest_reason_code(void)
{
    return control_context_last_reason_code()[0] != '\0' ? control_context_last_reason_code()
                                                        : "none";
}

#endif
