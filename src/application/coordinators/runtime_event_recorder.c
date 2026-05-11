#include "application/coordinators/runtime_event_recorder.h"

#include <string.h>

#include "domain/model/state_transition_record.h"
#include "src/application/coordinators/system_context_private.h"

static const char *safe_runtime_field(const char *value)
{
    return (value != 0 && value[0] != '\0') ? value : "none";
}

void runtime_event_recorder_set_latest_result(system_context_t system_context,
    const char *result_code,
    const char *reason_code)
{
    if (!system_context_private_require_active(system_context).ok) {
        return;
    }
    system_context_private_set_latest_result(system_context, result_code, reason_code);
}

void runtime_event_recorder_apply_projection(system_context_t system_context,
    const runtime_result_projection_t *runtime_result_projection)
{
    const event_logger_port_t *event_logger_port;
    state_transition_record_t *last_transition_record;

    if (!system_context_private_require_active(system_context).ok) {
        return;
    }
    if (runtime_result_projection == 0) {
        return;
    }

    if (runtime_result_projection->updates_latest_result) {
        runtime_event_recorder_set_latest_result(system_context,
            runtime_result_projection->latest_result_code,
            runtime_result_projection->latest_reason_code);
    }
    if (!runtime_result_projection->records_transition) {
        return;
    }

    last_transition_record = system_context_private_last_transition_record_mutable(system_context);
    event_logger_port = system_context_private_event_logger_port(system_context);
    if (last_transition_record == 0 || event_logger_port == 0) {
        return;
    }

    state_transition_record_init(last_transition_record,
        runtime_result_projection->transition_entity,
        safe_runtime_field(runtime_result_projection->entity_id),
        runtime_result_projection->trigger_type,
        safe_runtime_field(runtime_result_projection->previous_state),
        safe_runtime_field(runtime_result_projection->current_state),
        safe_runtime_field(runtime_result_projection->transition_result_code),
        safe_runtime_field(runtime_result_projection->transition_reason_code),
        system_context_current_time_ms(system_context));

    switch (runtime_result_projection->log_kind) {
        case RUNTIME_EVENT_LOG_TRANSITION:
            if (event_logger_port->log_transition != 0) {
                event_logger_port->log_transition(event_logger_port->context, last_transition_record);
            }
            break;
        case RUNTIME_EVENT_LOG_REJECTION:
            if (event_logger_port->log_rejection != 0) {
                event_logger_port->log_rejection(event_logger_port->context, last_transition_record);
            }
            break;
        case RUNTIME_EVENT_LOG_IGNORED:
            if (event_logger_port->log_ignored != 0) {
                event_logger_port->log_ignored(event_logger_port->context, last_transition_record);
            }
            break;
        case RUNTIME_EVENT_LOG_NONE:
        default:
            break;
    }
}
