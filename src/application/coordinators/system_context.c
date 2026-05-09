#include "application/coordinators/system_context.h"

#include <stdlib.h>
#include <string.h>

#include "domain/model/program_snapshot.h"
#include "domain/model/vehicle_type.h"
#include "domain/model/wait_condition.h"
#include "domain/model/wash_execution.h"
#include "domain/model/wash_program.h"
#include "domain/model/wash_session.h"
#include "domain/services/wait_timeout_service.h"
#include "src/application/coordinators/system_context_private.h"

static void reset_runtime_state(system_context_t *system_context)
{
    wash_program_init(&system_context->wash_program, "", "");
    vehicle_type_init(&system_context->vehicle_type, "", "");
    wash_session_reset(&system_context->wash_session);
    wash_execution_reset(&system_context->wash_execution);
    wait_condition_reset(&system_context->wait_condition);
    program_snapshot_reset(&system_context->program_snapshot);
    memset(&system_context->runtime_snapshot, 0, sizeof(system_context->runtime_snapshot));
    memset(&system_context->last_transition_record, 0, sizeof(system_context->last_transition_record));
    memset(&system_context->pending_triggers, 0, sizeof(system_context->pending_triggers));
    system_context->pending_trigger_count = 0u;
    system_context->current_time_ms = 0ul;
    system_context->next_session_sequence = 0ul;
    system_context->next_execution_sequence = 0ul;
    system_context->next_wait_condition_sequence = 0ul;
    system_context->global_fault_present = false;
    memset(system_context->global_fault_code, 0, sizeof(system_context->global_fault_code));
    memset(system_context->global_fault_reason, 0, sizeof(system_context->global_fault_reason));
    memset(system_context->last_result_code, 0, sizeof(system_context->last_result_code));
    memset(system_context->last_reason_code, 0, sizeof(system_context->last_reason_code));
}

system_context_t *system_context_create(void)
{
    system_context_t *system_context;

    system_context = (system_context_t *)malloc(sizeof(*system_context));
    if (system_context == 0) {
        return 0;
    }

    system_context_initialize(system_context);
    system_context->owns_storage = true;
    return system_context;
}

void system_context_initialize(system_context_t *system_context)
{
    if (system_context == 0) {
        return;
    }

    memset(system_context, 0, sizeof(*system_context));
    system_context->owns_storage = false;
    reset_runtime_state(system_context);
}

void system_context_destroy(system_context_t *system_context)
{
    bool owns_storage;

    if (system_context == 0) {
        return;
    }

    owns_storage = system_context->owns_storage;
    memset(system_context, 0, sizeof(*system_context));
    if (owns_storage) {
        free(system_context);
    }
}

void system_context_reset(system_context_t *system_context)
{
    bool owns_storage;
    sensor_port_t sensor_port;
    actuator_port_t actuator_port;
    event_logger_port_t event_logger_port;
    program_repository_port_t program_repository_port;

    if (system_context == 0) {
        return;
    }

    sensor_port = system_context->sensor_port;
    actuator_port = system_context->actuator_port;
    event_logger_port = system_context->event_logger_port;
    program_repository_port = system_context->program_repository_port;
    owns_storage = system_context->owns_storage;

    system_context_initialize(system_context);
    system_context->owns_storage = owns_storage;
    system_context->sensor_port = sensor_port;
    system_context->actuator_port = actuator_port;
    system_context->event_logger_port = event_logger_port;
    system_context->program_repository_port = program_repository_port;
}

void system_context_set_sensor_port(system_context_t *system_context, const sensor_port_t *sensor_port)
{
    if (system_context == 0) {
        return;
    }

    if (sensor_port == 0) {
        memset(&system_context->sensor_port, 0, sizeof(system_context->sensor_port));
        return;
    }

    system_context->sensor_port = *sensor_port;
}

void system_context_set_actuator_port(system_context_t *system_context, const actuator_port_t *actuator_port)
{
    if (system_context == 0) {
        return;
    }

    if (actuator_port == 0) {
        memset(&system_context->actuator_port, 0, sizeof(system_context->actuator_port));
        return;
    }

    system_context->actuator_port = *actuator_port;
}

void system_context_set_event_logger_port(system_context_t *system_context, const event_logger_port_t *event_logger_port)
{
    if (system_context == 0) {
        return;
    }

    if (event_logger_port == 0) {
        memset(&system_context->event_logger_port, 0, sizeof(system_context->event_logger_port));
        return;
    }

    system_context->event_logger_port = *event_logger_port;
}

void system_context_set_program_repository_port(system_context_t *system_context,
    const program_repository_port_t *program_repository_port)
{
    if (system_context == 0) {
        return;
    }

    if (program_repository_port == 0) {
        memset(&system_context->program_repository_port, 0, sizeof(system_context->program_repository_port));
        return;
    }

    system_context->program_repository_port = *program_repository_port;
}

const program_repository_port_t *system_context_program_repository_port(const system_context_t *system_context)
{
    if (system_context == 0) {
        return 0;
    }
    return &system_context->program_repository_port;
}

unsigned long system_context_current_time_ms(const system_context_t *system_context)
{
    if (system_context == 0) {
        return 0ul;
    }
    return system_context->current_time_ms;
}

unsigned int system_context_pending_trigger_count(const system_context_t *system_context)
{
    if (system_context == 0) {
        return 0u;
    }
    return system_context->pending_trigger_count;
}

unsigned int system_context_count_pending_triggers_by_id(const system_context_t *system_context, const char *trigger_id)
{
    unsigned int count;
    unsigned int index;

    if (system_context == 0 || trigger_id == 0 || trigger_id[0] == '\0') {
        return 0u;
    }

    count = 0u;
    for (index = 0u; index < system_context->pending_trigger_count; ++index) {
        if (strcmp(system_context->pending_triggers[index].trigger_id, trigger_id) == 0) {
            count += 1u;
        }
    }
    return count;
}

unsigned int system_context_count_pending_triggers_by_type(const system_context_t *system_context, trigger_type_t trigger_type)
{
    unsigned int count;
    unsigned int index;

    if (system_context == 0) {
        return 0u;
    }

    count = 0u;
    for (index = 0u; index < system_context->pending_trigger_count; ++index) {
        if (system_context->pending_triggers[index].trigger_type == trigger_type) {
            count += 1u;
        }
    }
    return count;
}

bool system_context_has_pending_runtime_work(const system_context_t *system_context)
{
    if (system_context == 0) {
        return false;
    }

    return system_context->pending_trigger_count > 0u
        || wait_timeout_service_should_fire(&system_context->wait_condition, system_context->current_time_ms);
}

bool system_context_wait_condition_active(const system_context_t *system_context)
{
    if (system_context == 0) {
        return false;
    }
    return system_context->wait_condition.active;
}

unsigned long system_context_wait_timeout_ms(const system_context_t *system_context)
{
    if (system_context == 0 || !system_context->wait_condition.active) {
        return 0ul;
    }
    if (system_context->current_time_ms >= system_context->wait_condition.timeout_at_ms) {
        return 0ul;
    }

    return system_context->wait_condition.timeout_at_ms - system_context->current_time_ms;
}

const char *system_context_last_result_code(const system_context_t *system_context)
{
    if (system_context == 0) {
        return "";
    }
    return system_context->last_result_code;
}

const char *system_context_last_reason_code(const system_context_t *system_context)
{
    if (system_context == 0) {
        return "";
    }
    return system_context->last_reason_code;
}
