#ifndef TESTS_TEST_SUPPORT_H
#define TESTS_TEST_SUPPORT_H

#include <stdio.h>
#include <string.h>

#include "adapters/logging/file_event_logger.h"
#include "adapters/outbound/file_program_repository.h"
#include "adapters/inbound/cli_command_adapter.h"
#include "application/coordinators/system_context.h"
#include "application/use_cases/query_wash_session_status.h"
#include "domain/model/program_snapshot.h"
#include "domain/model/wash_trigger_event.h"
#include "domain/services/program_snapshot_service.h"
#include "domain/services/wash_execution_service.h"
#include "domain/services/wash_session_state_machine.h"
#include "platform/drivers/simulated_brush_driver.h"
#include "platform/drivers/simulated_chemical_driver.h"
#include "platform/drivers/simulated_driver_context.h"
#include "platform/drivers/simulated_gantry_driver.h"
#include "platform/drivers/simulated_sensor_driver.h"
#include "platform/linux/main_loop.h"
#include "shared/error_codes.h"

#define TEST_ASSERT(expr) \
    do { \
        if (!(expr)) { \
            fprintf(stderr, "ASSERT FAILED: %s (%s:%d)\n", #expr, __FILE__, __LINE__); \
            return 1; \
        } \
    } while (0)

static inline void test_setup_system_context(system_context_t *system_context, simulated_driver_context_t *driver_context)
{
    memset(system_context, 0, sizeof(*system_context));
    simulated_driver_context_init(driver_context);
    simulated_sensor_driver_bind(&system_context->sensor_port, driver_context);
    simulated_gantry_driver_bind(&system_context->actuator_port, driver_context);
    simulated_brush_driver_bind(&system_context->actuator_port, driver_context);
    simulated_chemical_driver_bind(&system_context->actuator_port, driver_context);
    file_program_repository_init(system_context, "./configs");
    file_event_logger_init(system_context, "./runtime/logs/test_events.log");
}

static inline void test_assign_trigger_identity(system_context_t *system_context, wash_trigger_event_t *wash_trigger_event)
{
    if (system_context == 0 || wash_trigger_event == 0) {
        return;
    }

    snprintf(wash_trigger_event->trigger_id,
        sizeof(wash_trigger_event->trigger_id),
        "test-%lu-%u",
        system_context->current_time_ms,
        system_context->pending_trigger_count);
    strncpy(wash_trigger_event->source, "test-helper", sizeof(wash_trigger_event->source) - 1);
    wash_trigger_event->source[sizeof(wash_trigger_event->source) - 1] = '\0';
}

static inline unsigned int has_pending_trigger_count(system_context_t *system_context, const char *trigger_id)
{
    unsigned int index;
    unsigned int count;

    count = 0;
    for (index = 0; index < system_context->pending_trigger_count; ++index) {
        if (strcmp(system_context->pending_triggers[index].trigger_id, trigger_id) == 0) {
            count += 1;
        }
    }
    return count;
}

static inline operation_result_t test_submit_trigger_and_drain(system_context_t *system_context, wash_trigger_event_t *wash_trigger_event)
{
    operation_result_t result;

    if (system_context == 0 || wash_trigger_event == 0) {
        return operation_result_fail(ERROR_CODE_INVALID_ARGUMENT);
    }

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

static inline operation_result_t test_process_command(system_context_t *system_context,
    const char *command_line,
    char *response_line,
    size_t response_line_size)
{
    return cli_command_adapter_execute_formal_line(system_context, command_line, response_line, response_line_size);
}

static inline operation_result_t test_start_session(system_context_t *system_context, const char *program_id)
{
    char command_line[256];
    char response_line[512];

    snprintf(command_line, sizeof(command_line), "start %s", program_id != 0 ? program_id : "");
    return test_process_command(system_context, command_line, response_line, sizeof(response_line));
}

static inline operation_result_t test_submit_feedback(system_context_t *system_context, const char *feedback_code, const char *correlation_key)
{
    wash_trigger_event_t wash_trigger_event;

    wash_trigger_event_init(&wash_trigger_event,
        TRIGGER_TYPE_DEVICE_FEEDBACK,
        0,
        feedback_code,
        correlation_key,
        system_context->current_time_ms);
    return test_submit_trigger_and_drain(system_context, &wash_trigger_event);
}

static inline operation_result_t test_submit_fault_with_reason(system_context_t *system_context,
    const char *fault_code,
    const char *fault_reason)
{
    wash_trigger_event_t wash_trigger_event;

    wash_trigger_event_init(&wash_trigger_event,
        TRIGGER_TYPE_FAULT,
        0,
        fault_code,
        fault_reason != 0 ? fault_reason : "test-fault",
        system_context->current_time_ms);
    return test_submit_trigger_and_drain(system_context, &wash_trigger_event);
}

static inline operation_result_t test_submit_fault(system_context_t *system_context, const char *fault_code)
{
    return test_submit_fault_with_reason(system_context, fault_code, "test-fault");
}

static inline operation_result_t test_clear_fault(system_context_t *system_context)
{
    return test_submit_fault_with_reason(system_context, "clear", 0);
}

static inline operation_result_t test_submit_stop(system_context_t *system_context, const char *reason_code)
{
    wash_trigger_event_t wash_trigger_event;

    wash_trigger_event_init(&wash_trigger_event,
        TRIGGER_TYPE_STOP,
        0,
        reason_code,
        "stop-command",
        system_context->current_time_ms);
    return test_submit_trigger_and_drain(system_context, &wash_trigger_event);
}

static inline operation_result_t test_fire_timeout(system_context_t *system_context, unsigned long elapsed_ms)
{
    main_loop_advance_time(system_context, elapsed_ms);
    return main_loop_run(system_context);
}

static inline wash_session_service_args_t test_build_wash_session_service_args(system_context_t *system_context)
{
    wash_session_service_args_t wash_session_service_args;

    memset(&wash_session_service_args, 0, sizeof(wash_session_service_args));
    wash_session_service_args.wash_session = &system_context->wash_session;
    wash_session_service_args.program_snapshot = &system_context->program_snapshot;
    wash_session_service_args.next_session_sequence = &system_context->next_session_sequence;
    wash_session_service_args.current_time_ms = system_context->current_time_ms;
    return wash_session_service_args;
}

static inline program_snapshot_service_args_t test_build_program_snapshot_service_args(system_context_t *system_context)
{
    program_snapshot_service_args_t program_snapshot_service_args;

    memset(&program_snapshot_service_args, 0, sizeof(program_snapshot_service_args));
    program_snapshot_service_args.program_snapshot = &system_context->program_snapshot;
    program_snapshot_service_args.wash_program = &system_context->wash_program;
    program_snapshot_service_args.program_repository_port = &system_context->program_repository_port;
    program_snapshot_service_args.current_time_ms = system_context->current_time_ms;
    return program_snapshot_service_args;
}

static inline wash_execution_service_args_t test_build_wash_execution_service_args(system_context_t *system_context)
{
    wash_execution_service_args_t wash_execution_service_args;

    memset(&wash_execution_service_args, 0, sizeof(wash_execution_service_args));
    wash_execution_service_args.wash_execution = &system_context->wash_execution;
    wash_execution_service_args.wash_session = &system_context->wash_session;
    wash_execution_service_args.wait_condition = &system_context->wait_condition;
    wash_execution_service_args.program_snapshot = &system_context->program_snapshot;
    wash_execution_service_args.actuator_port = &system_context->actuator_port;
    wash_execution_service_args.next_execution_sequence = &system_context->next_execution_sequence;
    wash_execution_service_args.next_wait_condition_sequence = &system_context->next_wait_condition_sequence;
    wash_execution_service_args.current_time_ms = system_context->current_time_ms;
    return wash_execution_service_args;
}

static inline const char *test_latest_result_code(const system_context_t *system_context)
{
    return system_context->last_result_code[0] != '\0' ? system_context->last_result_code : "none";
}

static inline const char *test_latest_reason_code(const system_context_t *system_context)
{
    return system_context->last_reason_code[0] != '\0' ? system_context->last_reason_code : "none";
}

#endif
