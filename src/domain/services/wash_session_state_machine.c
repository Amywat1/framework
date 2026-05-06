#include "domain/services/wash_session_state_machine.h"

#include <string.h>

#include "domain/model/state_transition_record.h"
#include "shared/error_codes.h"

static void log_session_transition(system_context_t *system_context,
    const char *previous_state,
    const char *current_state,
    const char *result_code,
    const char *reason_code,
    trigger_type_t trigger_type)
{
    if (system_context == 0) {
        return;
    }

    if (result_code != 0) {
        strncpy(system_context->last_result_code, result_code, sizeof(system_context->last_result_code) - 1);
    }
    if (reason_code != 0) {
        strncpy(system_context->last_reason_code, reason_code, sizeof(system_context->last_reason_code) - 1);
    }
    state_transition_record_init(&system_context->last_transition_record,
        TRANSITION_ENTITY_SESSION,
        system_context->wash_session.session_id,
        trigger_type,
        previous_state,
        current_state,
        result_code,
        reason_code,
        system_context->current_time_ms);
    if (system_context->event_logger_port.log_transition != 0) {
        system_context->event_logger_port.log_transition(system_context->event_logger_port.context, &system_context->last_transition_record);
    }
}

operation_result_t wash_session_state_machine_start(system_context_t *system_context, const char *program_id)
{
    if (system_context == 0 || program_id == 0) {
        return operation_result_fail(ERROR_CODE_INVALID_ARGUMENT);
    }
    if (wash_session_is_running(&system_context->wash_session)) {
        return operation_result_fail(ERROR_CODE_INVALID_STATE);
    }

    system_context->next_session_sequence += 1;
    wash_session_create(&system_context->wash_session,
        program_id,
        system_context->program_snapshot.program_snapshot_id,
        system_context->current_time_ms,
        system_context->next_session_sequence);
    log_session_transition(system_context, "none", "created", "accepted", "none", TRIGGER_TYPE_START);
    wash_session_start_running(&system_context->wash_session);
    log_session_transition(system_context, "created", "running", "accepted", "none", TRIGGER_TYPE_START);
    return operation_result_ok();
}

operation_result_t wash_session_state_machine_complete(system_context_t *system_context, result_code_t result_code, trigger_type_t trigger_type)
{
    if (system_context == 0) {
        return operation_result_fail(ERROR_CODE_INVALID_ARGUMENT);
    }
    wash_session_complete(&system_context->wash_session, result_code, system_context->current_time_ms);
    log_session_transition(system_context, "running", "completed", "completed", "none", trigger_type);
    return operation_result_ok();
}

operation_result_t wash_session_state_machine_abort(system_context_t *system_context, result_code_t result_code, const char *reason_code, trigger_type_t trigger_type)
{
    if (system_context == 0) {
        return operation_result_fail(ERROR_CODE_INVALID_ARGUMENT);
    }
    wash_session_abort(&system_context->wash_session, result_code, reason_code, system_context->current_time_ms);
    log_session_transition(system_context, "running", "aborted", "aborted", reason_code, trigger_type);
    return operation_result_ok();
}
