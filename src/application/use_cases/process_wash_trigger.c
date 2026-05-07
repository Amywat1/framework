#include "application/use_cases/process_wash_trigger.h"

#include <string.h>

#include "application/coordinators/runtime_event_recorder.h"
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

    runtime_event_recorder_record(system_context,
        TRANSITION_ENTITY_REQUEST,
        system_context->wash_session.session_id[0] != '\0' ? system_context->wash_session.session_id : "request",
        trigger_type,
        ignored ? "received" : "submitted",
        ignored ? "ignored" : "rejected",
        result_code,
        reason_code,
        ignored ? RUNTIME_EVENT_LOG_IGNORED : RUNTIME_EVENT_LOG_REJECTION);
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
    runtime_event_recorder_set_latest_result(system_context, result_code, reason_code);
}

static void log_global_fault_record(system_context_t *system_context, const char *fault_code)
{
    if (system_context == 0) {
        return;
    }

    runtime_event_recorder_record(system_context,
        TRANSITION_ENTITY_REQUEST,
        fault_code != 0 ? fault_code : "global-fault",
        TRIGGER_TYPE_FAULT,
        "idle",
        "global_fault",
        "accepted",
        "global_fault_recorded",
        RUNTIME_EVENT_LOG_TRANSITION);
}

static void log_global_fault_clear(system_context_t *system_context, const char *fault_code)
{
    if (system_context == 0) {
        return;
    }

    runtime_event_recorder_record(system_context,
        TRANSITION_ENTITY_REQUEST,
        fault_code != 0 ? fault_code : "global-fault",
        TRIGGER_TYPE_FAULT,
        "global_fault",
        "idle",
        "accepted",
        "global_fault_cleared",
        RUNTIME_EVENT_LOG_TRANSITION);
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

    if (system_context->wash_execution.stage_index + 1 >= system_context->program_snapshot.frozen_program.stage_count) {
        result = wash_session_state_machine_complete(system_context, RESULT_CODE_SUCCESS, TRIGGER_TYPE_DEVICE_FEEDBACK);
        if (result.ok) {
            remember_runtime_result(system_context, "accepted", wash_trigger_event->signal_code);
        }
        return result;
    }
    result = wash_execution_service_begin_next_stage(system_context);
    if (!result.ok) {
        return wash_session_state_machine_abort(system_context, RESULT_CODE_SAFE_STOP, "dispatch_failed", TRIGGER_TYPE_DEVICE_FEEDBACK);
    }
    remember_runtime_result(system_context, "accepted", wash_trigger_event->signal_code);
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
    result = wash_session_state_machine_abort(system_context,
        RESULT_CODE_MANUAL_ABORT,
        wash_trigger_event->signal_code,
        TRIGGER_TYPE_STOP);
    if (result.ok) {
        remember_runtime_result(system_context, "accepted", wash_trigger_event->signal_code);
    }
    return result;
}

static operation_result_t handle_fault(system_context_t *system_context, const wash_trigger_event_t *wash_trigger_event)
{
    operation_result_t result;
    bool clear_requested;
    const char *fault_reason;
    char previous_fault_code[sizeof(system_context->global_fault_code)];

    clear_requested = strcmp(wash_trigger_event->signal_code, "clear") == 0;
    fault_reason = wash_trigger_event->correlation_key[0] != '\0'
        ? wash_trigger_event->correlation_key
        : wash_trigger_event->signal_code;

    if (!wash_session_is_running(&system_context->wash_session)) {
        if (clear_requested) {
            strncpy(previous_fault_code,
                system_context->global_fault_code,
                sizeof(previous_fault_code) - 1);
            previous_fault_code[sizeof(previous_fault_code) - 1] = '\0';
            system_context->global_fault_present = false;
            system_context->global_fault_code[0] = '\0';
            system_context->global_fault_reason[0] = '\0';
            log_global_fault_clear(system_context, previous_fault_code);
            return operation_result_ok();
        }

        system_context->global_fault_present = true;
        strncpy(system_context->global_fault_code,
            wash_trigger_event->signal_code,
            sizeof(system_context->global_fault_code) - 1);
        strncpy(system_context->global_fault_reason,
            fault_reason,
            sizeof(system_context->global_fault_reason) - 1);
        system_context->global_fault_code[sizeof(system_context->global_fault_code) - 1] = '\0';
        system_context->global_fault_reason[sizeof(system_context->global_fault_reason) - 1] = '\0';
        log_global_fault_record(system_context, wash_trigger_event->signal_code);
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
    result = wash_session_state_machine_abort(system_context,
        RESULT_CODE_SAFE_STOP,
        wash_trigger_event->signal_code,
        TRIGGER_TYPE_FAULT);
    if (result.ok) {
        remember_runtime_result(system_context, "accepted", wash_trigger_event->signal_code);
    }
    return result;
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
            result = wash_execution_service_handle_timeout_continue(system_context);
            if (!result.ok) {
                return result;
            }
            if (system_context->wash_execution.stage_index + 1 >= system_context->program_snapshot.frozen_program.stage_count) {
                result = wash_session_state_machine_complete(system_context, RESULT_CODE_DEGRADED_COMPLETE, TRIGGER_TYPE_TIMEOUT);
                if (result.ok) {
                    remember_runtime_result(system_context, "accepted", "timeout_continue");
                }
                return result;
            }
            result = wash_execution_service_begin_next_stage(system_context);
            if (!result.ok) {
                return wash_session_state_machine_abort(system_context, RESULT_CODE_SAFE_STOP, "dispatch_failed", TRIGGER_TYPE_TIMEOUT);
            }
            remember_runtime_result(system_context, "accepted", "timeout_continue");
            return result;
        case WAIT_TIMEOUT_RESOLUTION_FINISH_EXECUTION:
            result = wash_execution_service_handle_timeout_finish(system_context);
            if (!result.ok) {
                return result;
            }
            result = wash_session_state_machine_complete(system_context, RESULT_CODE_DEGRADED_COMPLETE, TRIGGER_TYPE_TIMEOUT);
            if (result.ok) {
                remember_runtime_result(system_context, "accepted", "timeout_finish");
            }
            return result;
        case WAIT_TIMEOUT_RESOLUTION_ABORT_SESSION:
            result = wash_execution_service_handle_timeout_abort(system_context);
            if (!result.ok) {
                return result;
            }
            result = wash_session_state_machine_abort(system_context, RESULT_CODE_SAFE_STOP, "timeout", TRIGGER_TYPE_TIMEOUT);
            if (result.ok) {
                remember_runtime_result(system_context, "accepted", "timeout");
            }
            return result;
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
