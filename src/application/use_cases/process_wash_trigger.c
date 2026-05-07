#include "application/use_cases/process_wash_trigger.h"

#include <string.h>

#include "application/coordinators/runtime_event_recorder.h"
#include "application/coordinators/runtime_result_projection.h"
#include "domain/services/precheck_service.h"
#include "domain/services/program_snapshot_service.h"
#include "domain/services/wait_timeout_service.h"
#include "domain/services/wash_execution_service.h"
#include "domain/services/wash_session_state_machine.h"
#include "shared/error_codes.h"

static wash_session_service_args_t build_session_service_args(system_context_t *system_context)
{
    wash_session_service_args_t wash_session_service_args;

    memset(&wash_session_service_args, 0, sizeof(wash_session_service_args));
    wash_session_service_args.wash_session = &system_context->wash_session;
    wash_session_service_args.program_snapshot = &system_context->program_snapshot;
    wash_session_service_args.next_session_sequence = &system_context->next_session_sequence;
    wash_session_service_args.current_time_ms = system_context->current_time_ms;
    return wash_session_service_args;
}

static program_snapshot_service_args_t build_program_snapshot_service_args(system_context_t *system_context)
{
    program_snapshot_service_args_t program_snapshot_service_args;

    memset(&program_snapshot_service_args, 0, sizeof(program_snapshot_service_args));
    program_snapshot_service_args.program_snapshot = &system_context->program_snapshot;
    program_snapshot_service_args.wash_program = &system_context->wash_program;
    program_snapshot_service_args.program_repository_port = &system_context->program_repository_port;
    program_snapshot_service_args.current_time_ms = system_context->current_time_ms;
    return program_snapshot_service_args;
}

static wash_execution_service_args_t build_execution_service_args(system_context_t *system_context)
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

static void apply_request_projection(system_context_t *system_context,
    trigger_type_t trigger_type,
    const char *entity_id,
    const char *previous_state,
    const char *current_state,
    const char *latest_result_code,
    const char *latest_reason_code,
    const char *transition_result_code,
    const char *transition_reason_code,
    runtime_event_log_kind_t runtime_event_log_kind)
{
    runtime_result_projection_t runtime_result_projection;

    runtime_result_projection_init(&runtime_result_projection);
    runtime_result_projection_set_latest_result(&runtime_result_projection, latest_result_code, latest_reason_code);
    runtime_result_projection_set_transition(&runtime_result_projection,
        TRANSITION_ENTITY_REQUEST,
        entity_id,
        trigger_type,
        previous_state,
        current_state,
        transition_result_code,
        transition_reason_code,
        runtime_event_log_kind);
    runtime_event_recorder_apply_projection(system_context, &runtime_result_projection);
}

static void apply_session_projection(system_context_t *system_context,
    trigger_type_t trigger_type,
    const wash_session_transition_fact_t *wash_session_transition_fact,
    const char *latest_result_code,
    const char *latest_reason_code)
{
    runtime_result_projection_t runtime_result_projection;

    if (wash_session_transition_fact == 0) {
        return;
    }

    runtime_result_projection_init(&runtime_result_projection);
    runtime_result_projection_set_latest_result(&runtime_result_projection,
        latest_result_code != 0 ? latest_result_code : wash_session_transition_fact->result_code,
        latest_reason_code != 0 ? latest_reason_code : wash_session_transition_fact->reason_code);
    runtime_result_projection_set_transition(&runtime_result_projection,
        TRANSITION_ENTITY_SESSION,
        wash_session_transition_fact->session_id,
        trigger_type,
        wash_session_transition_fact->previous_state,
        wash_session_transition_fact->current_state,
        wash_session_transition_fact->result_code,
        wash_session_transition_fact->reason_code,
        RUNTIME_EVENT_LOG_TRANSITION);
    runtime_event_recorder_apply_projection(system_context, &runtime_result_projection);
}

static void apply_execution_projection(system_context_t *system_context,
    trigger_type_t trigger_type,
    const wash_execution_fact_t *wash_execution_fact,
    const char *latest_result_code,
    const char *latest_reason_code)
{
    runtime_result_projection_t runtime_result_projection;

    if (wash_execution_fact == 0) {
        return;
    }

    runtime_result_projection_init(&runtime_result_projection);
    runtime_result_projection_set_latest_result(&runtime_result_projection,
        latest_result_code != 0 ? latest_result_code : wash_execution_fact->result_code,
        latest_reason_code != 0 ? latest_reason_code : wash_execution_fact->reason_code);
    runtime_result_projection_set_transition(&runtime_result_projection,
        TRANSITION_ENTITY_EXECUTION,
        wash_execution_fact->execution_id,
        trigger_type,
        wash_execution_fact->previous_state,
        wash_execution_fact->current_state,
        wash_execution_fact->result_code,
        wash_execution_fact->reason_code,
        RUNTIME_EVENT_LOG_TRANSITION);
    runtime_event_recorder_apply_projection(system_context, &runtime_result_projection);
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
        system_context->wash_session.last_correlation_key[
            sizeof(system_context->wash_session.last_correlation_key) - 1] = '\0';
    }
}

static void set_global_fault(system_context_t *system_context, const char *fault_code, const char *fault_reason)
{
    if (system_context == 0) {
        return;
    }

    system_context->global_fault_present = true;
    if (fault_code != 0) {
        strncpy(system_context->global_fault_code, fault_code, sizeof(system_context->global_fault_code) - 1);
        system_context->global_fault_code[sizeof(system_context->global_fault_code) - 1] = '\0';
    }
    if (fault_reason != 0) {
        strncpy(system_context->global_fault_reason, fault_reason, sizeof(system_context->global_fault_reason) - 1);
        system_context->global_fault_reason[sizeof(system_context->global_fault_reason) - 1] = '\0';
    }
}

static void clear_global_fault(system_context_t *system_context)
{
    if (system_context == 0) {
        return;
    }

    system_context->global_fault_present = false;
    system_context->global_fault_code[0] = '\0';
    system_context->global_fault_reason[0] = '\0';
}

static operation_result_t abort_after_dispatch_failure(system_context_t *system_context,
    trigger_type_t trigger_type,
    wash_session_service_args_t *wash_session_service_args,
    operation_result_t dispatch_result)
{
    operation_result_t abort_result;
    wash_session_transition_fact_t wash_session_transition_fact;

    abort_result = wash_session_state_machine_abort(wash_session_service_args,
        RESULT_CODE_SAFE_STOP,
        "dispatch_failed",
        &wash_session_transition_fact);
    if (abort_result.ok) {
        apply_session_projection(system_context,
            trigger_type,
            &wash_session_transition_fact,
            0,
            0);
        return dispatch_result;
    }

    return abort_result;
}

static operation_result_t handle_start(system_context_t *system_context, const wash_trigger_event_t *wash_trigger_event)
{
    program_snapshot_service_args_t program_snapshot_service_args;
    const char *reason_code;
    wash_execution_service_args_t wash_execution_service_args;
    operation_result_t result;
    wash_execution_fact_t wash_execution_fact;
    wash_session_service_args_t wash_session_service_args;
    wash_session_transition_fact_t wash_session_transition_fact;

    if (system_context->global_fault_present) {
        apply_request_projection(system_context,
            TRIGGER_TYPE_START,
            "request",
            "received",
            "rejected",
            "rejected",
            "global_fault_active",
            "rejected",
            "global_fault_active",
            RUNTIME_EVENT_LOG_REJECTION);
        return operation_result_fail(ERROR_CODE_INVALID_STATE);
    }
    if (wash_session_is_running(&system_context->wash_session)) {
        apply_request_projection(system_context,
            TRIGGER_TYPE_START,
            "request",
            "received",
            "rejected",
            "rejected",
            "running_session_exists",
            "rejected",
            "running_session_exists",
            RUNTIME_EVENT_LOG_REJECTION);
        return operation_result_fail(ERROR_CODE_INVALID_STATE);
    }

    result = precheck_service_run(&system_context->sensor_port);
    if (!result.ok) {
        reason_code = "precheck_failed";
        if (result.error_code == ERROR_CODE_SAFETY_INTERLOCK_ACTIVE) {
            reason_code = "safety_interlock_active";
        } else if (result.error_code == ERROR_CODE_VEHICLE_NOT_ALLOWED) {
            reason_code = "vehicle_not_allowed";
        }
        apply_request_projection(system_context,
            TRIGGER_TYPE_START,
            "request",
            "received",
            "rejected",
            "rejected",
            reason_code,
            "rejected",
            reason_code,
            RUNTIME_EVENT_LOG_REJECTION);
        return result;
    }

    program_snapshot_service_args = build_program_snapshot_service_args(system_context);
    result = program_snapshot_service_capture(&program_snapshot_service_args, wash_trigger_event->program_id);
    if (!result.ok) {
        reason_code = result.error_code == ERROR_CODE_RESOURCE_UNAVAILABLE ? "program_unavailable" : "program_invalid";
        apply_request_projection(system_context,
            TRIGGER_TYPE_START,
            "request",
            "received",
            "rejected",
            "rejected",
            reason_code,
            "rejected",
            reason_code,
            RUNTIME_EVENT_LOG_REJECTION);
        return result;
    }

    wash_session_service_args = build_session_service_args(system_context);
    result = wash_session_state_machine_start(&wash_session_service_args,
        wash_trigger_event->program_id,
        &wash_session_transition_fact);
    if (!result.ok) {
        return result;
    }

    wash_execution_reset(&system_context->wash_execution);
    system_context->wash_execution.stage_index = -1;
    system_context->wash_session.last_correlation_key[0] = '\0';
    wash_execution_service_args = build_execution_service_args(system_context);
    result = wash_execution_service_begin_next_stage(&wash_execution_service_args, &wash_execution_fact);
    if (!result.ok) {
        return abort_after_dispatch_failure(system_context,
            TRIGGER_TYPE_START,
            &wash_session_service_args,
            result);
    }

    apply_execution_projection(system_context,
        TRIGGER_TYPE_START,
        &wash_execution_fact,
        "accepted",
        "session_started");
    return operation_result_ok();
}

static operation_result_t handle_feedback(system_context_t *system_context, const wash_trigger_event_t *wash_trigger_event)
{
    wash_execution_service_args_t wash_execution_service_args;
    operation_result_t result;
    wash_execution_fact_t wash_execution_fact;
    wash_session_service_args_t wash_session_service_args;
    wash_session_transition_fact_t wash_session_transition_fact;

    if (wash_session_is_terminal(&system_context->wash_session)) {
        apply_request_projection(system_context,
            TRIGGER_TYPE_DEVICE_FEEDBACK,
            "request",
            "received",
            "ignored",
            "ignored",
            "late_event",
            "ignored",
            "late_event",
            RUNTIME_EVENT_LOG_IGNORED);
        return operation_result_ok();
    }
    if (!wash_session_is_running(&system_context->wash_session)) {
        apply_request_projection(system_context,
            TRIGGER_TYPE_DEVICE_FEEDBACK,
            "request",
            "received",
            "ignored",
            "ignored",
            "idle_feedback",
            "ignored",
            "idle_feedback",
            RUNTIME_EVENT_LOG_IGNORED);
        return operation_result_ok();
    }
    if (is_duplicate_event(system_context, wash_trigger_event)) {
        apply_request_projection(system_context,
            TRIGGER_TYPE_DEVICE_FEEDBACK,
            "request",
            "received",
            "ignored",
            "ignored",
            "duplicate_event",
            "ignored",
            "duplicate_event",
            RUNTIME_EVENT_LOG_IGNORED);
        return operation_result_ok();
    }

    wash_execution_service_args = build_execution_service_args(system_context);
    result = wash_execution_service_handle_feedback(&wash_execution_service_args,
        wash_trigger_event,
        &wash_execution_fact);
    if (!result.ok) {
        apply_request_projection(system_context,
            TRIGGER_TYPE_DEVICE_FEEDBACK,
            "request",
            "received",
            "ignored",
            "ignored",
            "unmatched_feedback",
            "ignored",
            "unmatched_feedback",
            RUNTIME_EVENT_LOG_IGNORED);
        return operation_result_ok();
    }

    remember_correlation_key(system_context, wash_trigger_event);
    if (system_context->wash_execution.stage_index + 1 >= system_context->program_snapshot.frozen_program.stage_count) {
        wash_session_service_args = build_session_service_args(system_context);
        result = wash_session_state_machine_complete(&wash_session_service_args,
            RESULT_CODE_SUCCESS,
            &wash_session_transition_fact);
        if (result.ok) {
            apply_session_projection(system_context,
                TRIGGER_TYPE_DEVICE_FEEDBACK,
                &wash_session_transition_fact,
                "accepted",
                wash_trigger_event->signal_code);
        }
        return result;
    }

    wash_execution_service_args = build_execution_service_args(system_context);
    result = wash_execution_service_begin_next_stage(&wash_execution_service_args, &wash_execution_fact);
    if (!result.ok) {
        wash_session_service_args = build_session_service_args(system_context);
        return abort_after_dispatch_failure(system_context,
            TRIGGER_TYPE_DEVICE_FEEDBACK,
            &wash_session_service_args,
            result);
    }

    apply_execution_projection(system_context,
        TRIGGER_TYPE_DEVICE_FEEDBACK,
        &wash_execution_fact,
        "accepted",
        wash_trigger_event->signal_code);
    return operation_result_ok();
}

static operation_result_t handle_stop(system_context_t *system_context, const wash_trigger_event_t *wash_trigger_event)
{
    wash_execution_service_args_t wash_execution_service_args;
    operation_result_t result;
    wash_execution_fact_t wash_execution_fact;
    wash_session_service_args_t wash_session_service_args;
    wash_session_transition_fact_t wash_session_transition_fact;

    if (!wash_session_is_running(&system_context->wash_session)) {
        apply_request_projection(system_context,
            TRIGGER_TYPE_STOP,
            "request",
            "received",
            "ignored",
            "ignored",
            "idle_stop",
            "ignored",
            "idle_stop",
            RUNTIME_EVENT_LOG_IGNORED);
        return operation_result_ok();
    }

    wash_execution_service_args = build_execution_service_args(system_context);
    result = wash_execution_service_handle_stop(&wash_execution_service_args,
        wash_trigger_event->signal_code,
        &wash_execution_fact);
    if (!result.ok) {
        return result;
    }

    wash_session_service_args = build_session_service_args(system_context);
    result = wash_session_state_machine_abort(&wash_session_service_args,
        RESULT_CODE_MANUAL_ABORT,
        wash_trigger_event->signal_code,
        &wash_session_transition_fact);
    if (result.ok) {
        apply_session_projection(system_context,
            TRIGGER_TYPE_STOP,
            &wash_session_transition_fact,
            "accepted",
            wash_trigger_event->signal_code);
    }
    return result;
}

static operation_result_t handle_fault(system_context_t *system_context, const wash_trigger_event_t *wash_trigger_event)
{
    bool clear_requested;
    const char *fault_reason;
    operation_result_t result;
    wash_execution_service_args_t wash_execution_service_args;
    wash_execution_fact_t wash_execution_fact;
    wash_session_service_args_t wash_session_service_args;
    wash_session_transition_fact_t wash_session_transition_fact;
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
            clear_global_fault(system_context);
            apply_request_projection(system_context,
                TRIGGER_TYPE_FAULT,
                previous_fault_code[0] != '\0' ? previous_fault_code : "global-fault",
                "global_fault",
                "idle",
                "accepted",
                "global_fault_cleared",
                "accepted",
                "global_fault_cleared",
                RUNTIME_EVENT_LOG_TRANSITION);
            return operation_result_ok();
        }

        set_global_fault(system_context, wash_trigger_event->signal_code, fault_reason);
        apply_request_projection(system_context,
            TRIGGER_TYPE_FAULT,
            wash_trigger_event->signal_code,
            "idle",
            "global_fault",
            "accepted",
            "global_fault_recorded",
            "accepted",
            "global_fault_recorded",
            RUNTIME_EVENT_LOG_TRANSITION);
        return operation_result_ok();
    }

    if (clear_requested) {
        apply_request_projection(system_context,
            TRIGGER_TYPE_FAULT,
            "request",
            "received",
            "rejected",
            "rejected",
            "fault_clear_while_running",
            "rejected",
            "fault_clear_while_running",
            RUNTIME_EVENT_LOG_REJECTION);
        return operation_result_fail(ERROR_CODE_INVALID_STATE);
    }

    wash_execution_service_args = build_execution_service_args(system_context);
    result = wash_execution_service_handle_fault(&wash_execution_service_args,
        wash_trigger_event->signal_code,
        &wash_execution_fact);
    if (!result.ok) {
        return result;
    }

    wash_session_service_args = build_session_service_args(system_context);
    result = wash_session_state_machine_abort(&wash_session_service_args,
        RESULT_CODE_SAFE_STOP,
        wash_trigger_event->signal_code,
        &wash_session_transition_fact);
    if (result.ok) {
        apply_session_projection(system_context,
            TRIGGER_TYPE_FAULT,
            &wash_session_transition_fact,
            "accepted",
            wash_trigger_event->signal_code);
    }
    return result;
}

static operation_result_t handle_timeout(system_context_t *system_context)
{
    operation_result_t result;
    wait_timeout_fact_t wait_timeout_fact;
    wash_execution_fact_t wash_execution_fact;
    wash_execution_service_args_t wash_execution_service_args;
    wash_session_service_args_t wash_session_service_args;
    wash_session_transition_fact_t wash_session_transition_fact;

    result = wait_timeout_service_handle_timeout(&system_context->wait_condition,
        system_context->current_time_ms,
        &wait_timeout_fact);
    if (!result.ok) {
        return result;
    }

    switch (wait_timeout_fact.resolution) {
        case WAIT_TIMEOUT_RESOLUTION_RETRIED:
        {
            runtime_result_projection_t runtime_result_projection;

            runtime_result_projection_init(&runtime_result_projection);
            runtime_result_projection_set_latest_result(&runtime_result_projection, "waiting", wait_timeout_fact.reason_code);
            runtime_result_projection_set_transition(&runtime_result_projection,
                TRANSITION_ENTITY_EXECUTION,
                system_context->wash_execution.execution_id,
                TRIGGER_TYPE_TIMEOUT,
                "running",
                "running",
                "waiting",
                wait_timeout_fact.reason_code,
                RUNTIME_EVENT_LOG_TRANSITION);
            runtime_event_recorder_apply_projection(system_context, &runtime_result_projection);
            return operation_result_ok();
        }
        case WAIT_TIMEOUT_RESOLUTION_CONTINUE_SESSION:
            wash_execution_service_args = build_execution_service_args(system_context);
            result = wash_execution_service_handle_timeout_continue(&wash_execution_service_args,
                &wash_execution_fact);
            if (!result.ok) {
                return result;
            }
            if (system_context->wash_execution.stage_index + 1 >= system_context->program_snapshot.frozen_program.stage_count) {
                wash_session_service_args = build_session_service_args(system_context);
                result = wash_session_state_machine_complete(&wash_session_service_args,
                    RESULT_CODE_DEGRADED_COMPLETE,
                    &wash_session_transition_fact);
                if (result.ok) {
                    apply_session_projection(system_context,
                        TRIGGER_TYPE_TIMEOUT,
                        &wash_session_transition_fact,
                        "accepted",
                        wait_timeout_fact.reason_code);
                }
                return result;
            }

            wash_execution_service_args = build_execution_service_args(system_context);
            result = wash_execution_service_begin_next_stage(&wash_execution_service_args, &wash_execution_fact);
            if (!result.ok) {
                wash_session_service_args = build_session_service_args(system_context);
                return abort_after_dispatch_failure(system_context,
                    TRIGGER_TYPE_TIMEOUT,
                    &wash_session_service_args,
                    result);
            }

            apply_execution_projection(system_context,
                TRIGGER_TYPE_TIMEOUT,
                &wash_execution_fact,
                "accepted",
                wait_timeout_fact.reason_code);
            return operation_result_ok();
        case WAIT_TIMEOUT_RESOLUTION_FINISH_EXECUTION:
            wash_execution_service_args = build_execution_service_args(system_context);
            result = wash_execution_service_handle_timeout_finish(&wash_execution_service_args,
                &wash_execution_fact);
            if (!result.ok) {
                return result;
            }
            wash_session_service_args = build_session_service_args(system_context);
            result = wash_session_state_machine_complete(&wash_session_service_args,
                RESULT_CODE_DEGRADED_COMPLETE,
                &wash_session_transition_fact);
            if (result.ok) {
                apply_session_projection(system_context,
                    TRIGGER_TYPE_TIMEOUT,
                    &wash_session_transition_fact,
                    "accepted",
                    wait_timeout_fact.reason_code);
            }
            return result;
        case WAIT_TIMEOUT_RESOLUTION_ABORT_SESSION:
            wash_execution_service_args = build_execution_service_args(system_context);
            result = wash_execution_service_handle_timeout_abort(&wash_execution_service_args,
                &wash_execution_fact);
            if (!result.ok) {
                return result;
            }
            wash_session_service_args = build_session_service_args(system_context);
            result = wash_session_state_machine_abort(&wash_session_service_args,
                RESULT_CODE_SAFE_STOP,
                "timeout",
                &wash_session_transition_fact);
            if (result.ok) {
                apply_session_projection(system_context,
                    TRIGGER_TYPE_TIMEOUT,
                    &wash_session_transition_fact,
                    "accepted",
                    wait_timeout_fact.reason_code);
            }
            return result;
        case WAIT_TIMEOUT_RESOLUTION_NONE:
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
