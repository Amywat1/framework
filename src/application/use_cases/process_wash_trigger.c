#include "application/use_cases/process_wash_trigger.h"

#include <string.h>

#include "domain/model/state_transition_record.h"
#include "domain/services/precheck_service.h"
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

    if (result_code != 0) {
        strncpy(system_context->last_result_code, result_code, sizeof(system_context->last_result_code) - 1);
    }
    if (reason_code != 0) {
        strncpy(system_context->last_reason_code, reason_code, sizeof(system_context->last_reason_code) - 1);
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

static void remember_runtime_result(system_context_t *system_context, const char *result_code, const char *reason_code)
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
}

static void log_global_fault_record(system_context_t *system_context, const char *fault_code, const char *fault_reason)
{
    if (system_context == 0) {
        return;
    }

    remember_runtime_result(system_context, "accepted", "global_fault_recorded");
    state_transition_record_init(&system_context->last_transition_record,
        TRANSITION_ENTITY_REQUEST,
        "global-fault",
        TRIGGER_TYPE_FAULT,
        "idle",
        "global_fault",
        fault_code != 0 ? fault_code : "fault",
        fault_reason != 0 ? fault_reason : "fault",
        system_context->current_time_ms);
    if (system_context->event_logger_port.log_transition != 0) {
        system_context->event_logger_port.log_transition(system_context->event_logger_port.context, &system_context->last_transition_record);
    }
}

static void log_global_fault_clear(system_context_t *system_context, const char *reason_code)
{
    if (system_context == 0) {
        return;
    }

    remember_runtime_result(system_context, "accepted", reason_code);
    state_transition_record_init(&system_context->last_transition_record,
        TRANSITION_ENTITY_REQUEST,
        "global-fault",
        TRIGGER_TYPE_FAULT,
        "global_fault",
        "idle",
        "cleared",
        reason_code,
        system_context->current_time_ms);
    if (system_context->event_logger_port.log_transition != 0) {
        system_context->event_logger_port.log_transition(system_context->event_logger_port.context, &system_context->last_transition_record);
    }
}

static operation_result_t handle_start(system_context_t *system_context, const wash_trigger_event_t *wash_trigger_event)
{
    operation_result_t result;

    if (system_context->global_fault_present) {
        log_request_record(system_context, TRIGGER_TYPE_START, "rejected", "global_fault_active", false);
        return operation_result_fail(ERROR_CODE_INVALID_STATE);
    }
    if (wash_session_is_running(&system_context->wash_session)) {
        log_request_record(system_context, TRIGGER_TYPE_START, "rejected", "running_session_exists", false);
        return operation_result_fail(ERROR_CODE_INVALID_STATE);
    }
    result = precheck_service_run(&system_context->sensor_port);
    if (!result.ok) {
        const char *reason_code = "precheck_failed";

        if (result.error_code == ERROR_CODE_SAFETY_INTERLOCK_ACTIVE) {
            reason_code = "safety_interlock_active";
        } else if (result.error_code == ERROR_CODE_VEHICLE_NOT_ALLOWED) {
            reason_code = "vehicle_not_allowed";
        }
        log_request_record(system_context, TRIGGER_TYPE_START, "rejected", reason_code, false);
        return result;
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
    remember_runtime_result(system_context, "accepted", "session_started");
    return result;
}

static operation_result_t handle_feedback(system_context_t *system_context, const wash_trigger_event_t *wash_trigger_event)
{
    operation_result_t result;

    if (wash_session_is_terminal(&system_context->wash_session)) {
        log_request_record(system_context, TRIGGER_TYPE_DEVICE_FEEDBACK, "ignored", "late_event", true);
        return operation_result_ok();
    }
    if (!wash_session_is_running(&system_context->wash_session)) {
        log_request_record(system_context, TRIGGER_TYPE_DEVICE_FEEDBACK, "ignored", "idle_feedback", true);
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
    remember_runtime_result(system_context, "accepted", wash_trigger_event->signal_code);

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
        log_request_record(system_context, TRIGGER_TYPE_STOP, "ignored", "idle_stop", true);
        return operation_result_ok();
    }
    result = wash_execution_service_handle_stop(system_context, wash_trigger_event->signal_code);
    if (!result.ok) {
        return result;
    }
    remember_runtime_result(system_context, "accepted", wash_trigger_event->signal_code);
    return wash_session_state_machine_abort(system_context, RESULT_CODE_MANUAL_ABORT, wash_trigger_event->signal_code, TRIGGER_TYPE_STOP);
}

static operation_result_t handle_fault(system_context_t *system_context, const wash_trigger_event_t *wash_trigger_event)
{
    operation_result_t result;
    bool clear_requested;
    const char *fault_reason;

    clear_requested = strcmp(wash_trigger_event->signal_code, "clear") == 0;
    fault_reason = wash_trigger_event->correlation_key[0] != '\0'
        ? wash_trigger_event->correlation_key
        : wash_trigger_event->signal_code;

    if (!wash_session_is_running(&system_context->wash_session)) {
        if (clear_requested) {
            system_context->global_fault_present = false;
            system_context->global_fault_code[0] = '\0';
            system_context->global_fault_reason[0] = '\0';
            log_global_fault_clear(system_context, "global_fault_cleared");
            return operation_result_ok();
        }

        system_context->global_fault_present = true;
        strncpy(system_context->global_fault_code,
            wash_trigger_event->signal_code,
            sizeof(system_context->global_fault_code) - 1);
        strncpy(system_context->global_fault_reason,
            fault_reason,
            sizeof(system_context->global_fault_reason) - 1);
        log_global_fault_record(system_context, wash_trigger_event->signal_code, fault_reason);
        return operation_result_ok();
    }
    if (clear_requested) {
        log_request_record(system_context, TRIGGER_TYPE_FAULT, "rejected", "fault_clear_while_running", false);
        return operation_result_fail(ERROR_CODE_INVALID_STATE);
    }
    result = wash_execution_service_handle_fault(system_context, wash_trigger_event->signal_code);
    if (!result.ok) {
        return result;
    }
    remember_runtime_result(system_context, "accepted", wash_trigger_event->signal_code);
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
            remember_runtime_result(system_context, "waiting", "timeout_retry");
            return operation_result_ok();
        case WAIT_TIMEOUT_RESOLUTION_CONTINUE_SESSION:
            if (system_context->wash_execution.stage_index + 1 >= system_context->program_snapshot.frozen_program.stage_count) {
                return wash_session_state_machine_complete(system_context, RESULT_CODE_DEGRADED_COMPLETE, TRIGGER_TYPE_TIMEOUT);
            }
            result = wash_execution_service_begin_next_stage(system_context);
            if (!result.ok) {
                return wash_session_state_machine_abort(system_context, RESULT_CODE_SAFE_STOP, "dispatch_failed", TRIGGER_TYPE_TIMEOUT);
            }
            remember_runtime_result(system_context, "accepted", "timeout_continue");
            return result;
        case WAIT_TIMEOUT_RESOLUTION_FINISH_EXECUTION:
            remember_runtime_result(system_context, "accepted", "timeout_finish");
            return wash_session_state_machine_complete(system_context, RESULT_CODE_DEGRADED_COMPLETE, TRIGGER_TYPE_TIMEOUT);
        case WAIT_TIMEOUT_RESOLUTION_ABORT_SESSION:
            remember_runtime_result(system_context, "accepted", "timeout");
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
