#include "application/coordinators/runtime_event_recorder.h"

#include <string.h>

#include "domain/model/state_transition_record.h"

static const char *safe_runtime_field(const char *value)
{
    return (value != 0 && value[0] != '\0') ? value : "none";
}

void runtime_event_recorder_set_latest_result(system_context_t *system_context,
    const char *result_code,
    const char *reason_code)
{
    if (system_context == 0) {
        return;
    }
    if (result_code != 0) {
        strncpy(system_context->last_result_code, result_code, sizeof(system_context->last_result_code) - 1);
        system_context->last_result_code[sizeof(system_context->last_result_code) - 1] = '\0';
    }
    if (reason_code != 0) {
        strncpy(system_context->last_reason_code, reason_code, sizeof(system_context->last_reason_code) - 1);
        system_context->last_reason_code[sizeof(system_context->last_reason_code) - 1] = '\0';
    }
}

void runtime_event_recorder_record(system_context_t *system_context,
    transition_entity_type_t transition_entity,
    const char *entity_id,
    trigger_type_t trigger_type,
    const char *previous_state,
    const char *current_state,
    const char *result_code,
    const char *reason_code,
    runtime_event_log_kind_t runtime_event_log_kind)
{
    if (system_context == 0) {
        return;
    }

    runtime_event_recorder_set_latest_result(system_context, result_code, reason_code);
    state_transition_record_init(&system_context->last_transition_record,
        transition_entity,
        safe_runtime_field(entity_id),
        trigger_type,
        safe_runtime_field(previous_state),
        safe_runtime_field(current_state),
        safe_runtime_field(result_code),
        safe_runtime_field(reason_code),
        system_context->current_time_ms);

    switch (runtime_event_log_kind) {
        case RUNTIME_EVENT_LOG_TRANSITION:
            if (system_context->event_logger_port.log_transition != 0) {
                system_context->event_logger_port.log_transition(system_context->event_logger_port.context, &system_context->last_transition_record);
            }
            break;
        case RUNTIME_EVENT_LOG_REJECTION:
            if (system_context->event_logger_port.log_rejection != 0) {
                system_context->event_logger_port.log_rejection(system_context->event_logger_port.context, &system_context->last_transition_record);
            }
            break;
        case RUNTIME_EVENT_LOG_IGNORED:
            if (system_context->event_logger_port.log_ignored != 0) {
                system_context->event_logger_port.log_ignored(system_context->event_logger_port.context, &system_context->last_transition_record);
            }
            break;
        case RUNTIME_EVENT_LOG_NONE:
        default:
            break;
    }
}
