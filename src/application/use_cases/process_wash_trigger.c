#include "application/use_cases/process_wash_trigger.h"

#include <string.h>

#include "domain/model/state_transition_record.h"
#include "domain/services/program_snapshot_service.h"
#include "domain/services/wait_timeout_service.h"
#include "domain/services/wash_execution_service.h"
#include "domain/services/wash_session_state_machine.h"
#include "shared/error_codes.h"

static void log_request_record(system_context_t *system_context,
    trigger_type_t trigger_type,
    const char *result_code,
    const char *reason_code,
    bool ignored)
{
    if (system_context == 0) {
        return;
    }

    state_transition_record_init(&system_context->last_transition_record,
        TRANSITION_ENTITY_REQUEST,
        system_context->wash_session.session_id[0] != '\0' ? system_context->wash_session.session_id : "request",
        trigger_type,
        ignored ? "received" : "submitted",
        ignored ? "ignored" : "rejected",
        result_code,
        reason_code,
        system_context->current_time_ms);
    if (ignored && system_context->event_logger_port.log_ignored != 0) {
        system_context->event_logger_port.log_ignored(system_context->event_logger_port.context, &system_context->last_transition_record);
    } else if (!ignored && system_context->event_logger_port.log_rejection != 0) {
        system_context->event_logger_port.log_rejection(system_context->event_logger_port.context, &system_context->last_transition_record);
    }
}

static bool is_duplicate_event(system_context_t *system_context, const wash_trigger_event_t *wash_trigger_event)
{
    if (system_context == 0 || wash_trigger_event == 0) {
        return false;
    }
    if (wash_trigger_event->correlation_key[0] == '\0') {
        return false;
    }
    return strcmp(system_context->wash_session.last_correlation_key, wash_trigger_event->correlation_key) == 0;
}

static void remember_correlation_key(system_context_t *system_context, const wash_trigger_event_t *wash_trigger_event)
{
    if (system_context == 0 || wash_trigger_event == 0) {
        return;
    }
    if (wash_trigger_event->correlation_key[0] != '\0') {
        strncpy(system_context->wash_session.last_correlation_key,
            wash_trigger_event->correlation_key,
            sizeof(system_context->wash_session.last_correlation_key) - 1);
    }
}

static operation_result_t handle_start(system_context_t *system_context, const wash_trigger_event_t *wash_trigger_event)
{
    operation_result_t result;

    if (wash_session_is_running(&system_context->wash_session)) {
        log_request_record(system_context, TRIGGER_TYPE_START, "rejected", "running_session_exists", false);
        return operation_result_fail(ERROR_CODE_INVALID_STATE);
    }

    result = program_snapshot_service_capture(system_context, wash_trigger_event->program_id);
    if (!result.ok) {
        log_request_record(system_context,
            TRIGGER_TYPE_START,
            "rejected",
            result.error_code == ERROR_CODE_RESOURCE_UNAVAILABLE ? "program_unavailable" : "program_invalid",
            false);
        return result;
    }
    result = wash_session_state_machine_start(system_context, wash_trigger_event->program_id);
    if (!result.ok) {
        return result;
    }
    wash_execution_reset(&system_context->wash_execution);
    system_context->wash_execution.stage_index = -1;
    system_context->wash_session.last_correlation_key[0] = '\0';
    result = wash_execution_service_begin_next_stage(system_context);
    if (!result.ok) {
        return wash_session_state_machine_abort(system_context, RESULT_CODE_SAFE_STOP, "dispatch_failed", TRIGGER_TYPE_START);
    }
    return result;
}

static operation_result_t handle_feedback(system_context_t *system_context, const wash_trigger_event_t *wash_trigger_event)
{
    operation_result_t result;

    if (wash_session_is_terminal(&system_context->wash_session)) {
        log_request_record(system_context, TRIGGER_TYPE_DEVICE_FEEDBACK, "ignored", "late_event", true);
        return operation_result_ok();
    }
    if (is_duplicate_event(system_context, wash_trigger_event)) {
        log_request_record(system_context, TRIGGER_TYPE_DEVICE_FEEDBACK, "ignored", "duplicate_event", true);
        return operation_result_ok();
    }

    result = wash_execution_service_handle_feedback(system_context, wash_trigger_event);
    if (!result.ok) {
        log_request_record(system_context, TRIGGER_TYPE_DEVICE_FEEDBACK, "ignored", "unmatched_feedback", true);
        return operation_result_ok();
    }
    remember_correlation_key(system_context, wash_trigger_event);

    if (system_context->wash_execution.stage_index + 1 >= system_context->program_snapshot.frozen_program.stage_count) {
        return wash_session_state_machine_complete(system_context, RESULT_CODE_SUCCESS, TRIGGER_TYPE_DEVICE_FEEDBACK);
    }
    result = wash_execution_service_begin_next_stage(system_context);
    if (!result.ok) {
        return wash_session_state_machine_abort(system_context, RESULT_CODE_SAFE_STOP, "dispatch_failed", TRIGGER_TYPE_DEVICE_FEEDBACK);
    }
    return result;
}

static operation_result_t handle_stop(system_context_t *system_context, const wash_trigger_event_t *wash_trigger_event)
{
    operation_result_t result;

    if (!wash_session_is_running(&system_context->wash_session)) {
        log_request_record(system_context, TRIGGER_TYPE_STOP, "rejected", "invalid_state", false);
        return operation_result_fail(ERROR_CODE_INVALID_STATE);
    }
    result = wash_execution_service_handle_stop(system_context, wash_trigger_event->signal_code);
    if (!result.ok) {
        return result;
    }
    return wash_session_state_machine_abort(system_context, RESULT_CODE_MANUAL_ABORT, wash_trigger_event->signal_code, TRIGGER_TYPE_STOP);
}

static operation_result_t handle_fault(system_context_t *system_context, const wash_trigger_event_t *wash_trigger_event)
{
    operation_result_t result;

    if (!wash_session_is_running(&system_context->wash_session)) {
        log_request_record(system_context, TRIGGER_TYPE_FAULT, "rejected", "invalid_state", false);
        return operation_result_fail(ERROR_CODE_INVALID_STATE);
    }
    result = wash_execution_service_handle_fault(system_context, wash_trigger_event->signal_code);
    if (!result.ok) {
        return result;
    }
    return wash_session_state_machine_abort(system_context, RESULT_CODE_SAFE_STOP, wash_trigger_event->signal_code, TRIGGER_TYPE_FAULT);
}

static operation_result_t handle_timeout(system_context_t *system_context)
{
    operation_result_t result;
    wait_timeout_resolution_t wait_timeout_resolution;

    result = wait_timeout_service_handle_timeout(system_context, &wait_timeout_resolution);
    if (!result.ok) {
        return result;
    }
    switch (wait_timeout_resolution) {
        case WAIT_TIMEOUT_RESOLUTION_RETRIED:
            return operation_result_ok();
        case WAIT_TIMEOUT_RESOLUTION_CONTINUE_SESSION:
            if (system_context->wash_execution.stage_index + 1 >= system_context->program_snapshot.frozen_program.stage_count) {
                return wash_session_state_machine_complete(system_context, RESULT_CODE_DEGRADED_COMPLETE, TRIGGER_TYPE_TIMEOUT);
            }
            result = wash_execution_service_begin_next_stage(system_context);
            if (!result.ok) {
                return wash_session_state_machine_abort(system_context, RESULT_CODE_SAFE_STOP, "dispatch_failed", TRIGGER_TYPE_TIMEOUT);
            }
            return result;
        case WAIT_TIMEOUT_RESOLUTION_FINISH_EXECUTION:
            return wash_session_state_machine_complete(system_context, RESULT_CODE_DEGRADED_COMPLETE, TRIGGER_TYPE_TIMEOUT);
        case WAIT_TIMEOUT_RESOLUTION_ABORT_SESSION:
            return wash_session_state_machine_abort(system_context, RESULT_CODE_SAFE_STOP, "timeout", TRIGGER_TYPE_TIMEOUT);
        default:
            return operation_result_fail(ERROR_CODE_INVALID_STATE);
    }
}

operation_result_t process_wash_trigger_execute(system_context_t *system_context, const wash_trigger_event_t *wash_trigger_event)
{
    if (system_context == 0 || wash_trigger_event == 0) {
        return operation_result_fail(ERROR_CODE_INVALID_ARGUMENT);
    }

    switch (wash_trigger_event->trigger_type) {
        case TRIGGER_TYPE_START:
            return handle_start(system_context, wash_trigger_event);
        case TRIGGER_TYPE_DEVICE_FEEDBACK:
            return handle_feedback(system_context, wash_trigger_event);
        case TRIGGER_TYPE_STOP:
            return handle_stop(system_context, wash_trigger_event);
        case TRIGGER_TYPE_FAULT:
            return handle_fault(system_context, wash_trigger_event);
        case TRIGGER_TYPE_TIMEOUT:
            return handle_timeout(system_context);
        default:
            return operation_result_fail(ERROR_CODE_UNSUPPORTED);
    }
}
