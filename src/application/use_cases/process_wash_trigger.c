#include "application/use_cases/process_wash_trigger.h"

#include <string.h>

#include "application/coordinators/runtime_event_recorder.h"
#include "application/coordinators/runtime_result_projection.h"
#include "domain/services/program_snapshot_service.h"
#include "domain/services/recovery_state_machine.h"
#include "domain/services/wait_timeout_service.h"
#include "domain/services/wash_execution_service.h"
#include "domain/services/wash_session_state_machine.h"
#include "src/application/coordinators/system_context_private.h"
#include "shared/error_codes.h"

static wash_session_service_args_t build_session_service_args(system_context_t system_context)
{
    wash_session_service_args_t wash_session_service_args;

    system_context_private_build_session_service_args(system_context, &wash_session_service_args);
    return wash_session_service_args;
}

static program_snapshot_service_args_t build_program_snapshot_service_args(system_context_t system_context)
{
    program_snapshot_service_args_t program_snapshot_service_args;

    system_context_private_build_program_snapshot_service_args(system_context, &program_snapshot_service_args);
    return program_snapshot_service_args;
}

static wash_execution_service_args_t build_execution_service_args(system_context_t system_context)
{
    wash_execution_service_args_t wash_execution_service_args;

    system_context_private_build_execution_service_args(system_context, &wash_execution_service_args);
    return wash_execution_service_args;
}

static void set_latest_result(system_context_t system_context, const char *result_code, const char *reason_code)
{
    runtime_event_recorder_set_latest_result(system_context, result_code, reason_code);
}

static const char *device_state_to_string(device_state_t device_state)
{
    switch (device_state) {
        case DEVICE_STATE_INIT:
            return "init";
        case DEVICE_STATE_RECOVERING:
            return "recovering";
        case DEVICE_STATE_IDLE:
            return "idle";
        case DEVICE_STATE_RUNNING:
            return "running";
        case DEVICE_STATE_EXCEPTION:
            return "exception";
        case DEVICE_STATE_STOPPED:
        default:
            return "stopped";
    }
}

static void project_trigger_result(system_context_t system_context,
    const wash_trigger_event_t *wash_trigger_event,
    device_state_t previous_device_state,
    device_state_t current_device_state,
    const char *result_code,
    const char *reason_code,
    runtime_event_log_kind_t runtime_event_log_kind)
{
    runtime_result_projection_t runtime_result_projection;

    if (wash_trigger_event == 0) {
        set_latest_result(system_context, result_code, reason_code);
        return;
    }

    runtime_result_projection_init(&runtime_result_projection);
    runtime_result_projection_set_latest_result(&runtime_result_projection, result_code, reason_code);
    runtime_result_projection_set_transition(&runtime_result_projection,
        TRANSITION_ENTITY_REQUEST,
        wash_trigger_event->trigger_id[0] != '\0' ? wash_trigger_event->trigger_id : "runtime-trigger",
        wash_trigger_event->trigger_type,
        device_state_to_string(previous_device_state),
        device_state_to_string(current_device_state),
        result_code,
        reason_code,
        runtime_event_log_kind);
    runtime_event_recorder_apply_projection(system_context, &runtime_result_projection);
}

static void set_device_state(system_context_t system_context, device_state_t device_state)
{
    system_context_private_set_device_state(system_context, device_state);
}

static const char *stop_rejection_reason(void)
{
    return "stop_requires_running";
}

static const char *fault_clear_rejection_reason(void)
{
    return "fault_clear_requires_exception";
}

static result_code_t map_execution_abort_result(const wash_execution_t *wash_execution)
{
    if (wash_execution == 0) {
        return RESULT_CODE_SAFE_STOP;
    }
    if (wash_execution->execution_result == EXECUTION_RESULT_EXIT_TIMEOUT
        || wash_execution->execution_result == EXECUTION_RESULT_EXIT_FAILED) {
        return RESULT_CODE_EXIT_FAILED;
    }
    if (wash_execution->execution_result == EXECUTION_RESULT_SEGMENT_TIMEOUT) {
        return RESULT_CODE_SEGMENT_TIMEOUT;
    }
    if (wash_execution->execution_result == EXECUTION_RESULT_STOPPED) {
        return RESULT_CODE_MANUAL_ABORT;
    }
    return RESULT_CODE_SAFE_STOP;
}

static operation_result_t advance_or_finish_session(system_context_t system_context)
{
    const program_snapshot_t *program_snapshot;
    const wash_execution_t *wash_execution;
    const wash_session_t *wash_session;
    operation_result_t result;
    wash_execution_service_args_t wash_execution_service_args;
    wash_execution_fact_t wash_execution_fact;
    wash_session_service_args_t wash_session_service_args;
    wash_session_transition_fact_t wash_session_transition_fact;

    wash_execution = system_context_private_wash_execution(system_context);
    wash_session = system_context_private_wash_session(system_context);
    program_snapshot = system_context_private_program_snapshot(system_context);
    if (wash_execution == 0 || wash_session == 0 || program_snapshot == 0) {
        return operation_result_fail(ERROR_CODE_INVALID_STATE);
    }

    if (wash_execution->execution_state == EXECUTION_STATE_ABORTED
        && wash_session_is_running(wash_session)) {
        wash_session_service_args = build_session_service_args(system_context);
        result = wash_session_state_machine_abort(&wash_session_service_args,
            map_execution_abort_result(wash_execution),
            wash_execution->reason_code[0] != '\0'
                ? wash_execution->reason_code
                : wash_session->abort_reason,
            &wash_session_transition_fact);
        if (result.ok) {
            set_device_state(system_context,
                wash_execution->execution_result == EXECUTION_RESULT_STOPPED
                    ? DEVICE_STATE_STOPPED
                    : DEVICE_STATE_EXCEPTION);
            set_latest_result(system_context, "aborted", wash_session_transition_fact.reason_code);
        }
        return result;
    }

    if (wash_execution->execution_state != EXECUTION_STATE_COMPLETED
        || wash_execution->lifecycle_state != SEGMENT_LIFECYCLE_COMPLETED) {
        return operation_result_ok();
    }

    if (wash_execution->segment_index + 1
        < program_snapshot->frozen_program.segment_count) {
        wash_execution_service_args = build_execution_service_args(system_context);
        result = wash_execution_service_begin_next_segment(&wash_execution_service_args, &wash_execution_fact);
        if (result.ok) {
            set_latest_result(system_context, wash_execution_fact.result_code, wash_execution_fact.reason_code);
        }
        return result;
    }

    wash_session_service_args = build_session_service_args(system_context);
    result = wash_session_state_machine_complete(&wash_session_service_args,
        RESULT_CODE_SUCCESS,
        &wash_session_transition_fact);
    if (result.ok) {
        set_device_state(system_context, DEVICE_STATE_IDLE);
        set_latest_result(system_context, "completed", "program_finished");
    }
    return result;
}

static operation_result_t handle_homing(system_context_t system_context)
{
    const wash_session_t *wash_session;
    const actuator_port_t *actuator_port;
    const sensor_port_t *sensor_port;
    const char *failure_reason_code;
    wash_trigger_event_t wash_trigger_event;
    device_state_t device_state;
    operation_result_t result;

    wash_session = system_context_private_wash_session(system_context);
    actuator_port = system_context_private_actuator_port(system_context);
    sensor_port = system_context_private_sensor_port(system_context);
    if (wash_session == 0 || actuator_port == 0 || sensor_port == 0) {
        return operation_result_fail(ERROR_CODE_INVALID_STATE);
    }
    device_state = system_context_private_device_state(system_context);
    if (wash_session_is_running(wash_session)) {
        wash_trigger_event_init(&wash_trigger_event,
            TRIGGER_TYPE_HOMING,
            0,
            "homing",
            "homing-command",
            system_context_current_time_ms(system_context));
        project_trigger_result(system_context,
            &wash_trigger_event,
            device_state,
            device_state,
            "rejected",
            "running_session_exists",
            RUNTIME_EVENT_LOG_REJECTION);
        return operation_result_fail(ERROR_CODE_INVALID_STATE);
    }
    if (device_state != DEVICE_STATE_STOPPED) {
        wash_trigger_event_init(&wash_trigger_event,
            TRIGGER_TYPE_HOMING,
            0,
            "homing",
            "homing-command",
            system_context_current_time_ms(system_context));
        project_trigger_result(system_context,
            &wash_trigger_event,
            device_state,
            device_state,
            "rejected",
            "homing_requires_stopped",
            RUNTIME_EVENT_LOG_REJECTION);
        return operation_result_fail(ERROR_CODE_INVALID_STATE);
    }

    wash_trigger_event_init(&wash_trigger_event,
        TRIGGER_TYPE_HOMING,
        0,
        "homing",
        "homing-command",
        system_context_current_time_ms(system_context));
    system_context_private_clear_global_fault(system_context);
    set_device_state(system_context, DEVICE_STATE_RECOVERING);
    failure_reason_code = 0;
    result = recovery_state_machine_execute(actuator_port,
        sensor_port,
        RECOVERY_MODE_HOME_ROOF_BRUSH,
        &failure_reason_code);
    if (!result.ok || system_context_private_global_fault_present(system_context)) {
        set_device_state(system_context, DEVICE_STATE_EXCEPTION);
        project_trigger_result(system_context,
            &wash_trigger_event,
            DEVICE_STATE_STOPPED,
            DEVICE_STATE_EXCEPTION,
            "error",
            failure_reason_code != 0
                ? failure_reason_code
                : (system_context_private_global_fault_present(system_context)
                        ? "recovering_alarm_triggered"
                        : "homing_failed"),
            RUNTIME_EVENT_LOG_TRANSITION);
        return !result.ok ? result : operation_result_fail(ERROR_CODE_INVALID_STATE);
    }

    set_device_state(system_context, DEVICE_STATE_IDLE);
    project_trigger_result(system_context,
        &wash_trigger_event,
        DEVICE_STATE_STOPPED,
        DEVICE_STATE_IDLE,
        "accepted",
        "homing_completed",
        RUNTIME_EVENT_LOG_TRANSITION);
    return operation_result_ok();
}

static operation_result_t handle_start(system_context_t system_context, const wash_trigger_event_t *wash_trigger_event)
{
    device_state_t device_state;
    operation_result_t result;
    const char *reason_code;
    program_snapshot_service_args_t program_snapshot_service_args;
    wash_execution_t *wash_execution;
    wash_session_t *wash_session;
    wash_session_service_args_t wash_session_service_args;
    wash_session_transition_fact_t wash_session_transition_fact;
    wash_execution_service_args_t wash_execution_service_args;
    wash_execution_fact_t wash_execution_fact;

    device_state = system_context_private_device_state(system_context);
    if (device_state != DEVICE_STATE_IDLE) {
        project_trigger_result(system_context,
            wash_trigger_event,
            device_state,
            device_state,
            "rejected",
            "device_not_idle",
            RUNTIME_EVENT_LOG_REJECTION);
        return operation_result_fail(ERROR_CODE_INVALID_STATE);
    }
    if (system_context_private_global_fault_present(system_context)) {
        project_trigger_result(system_context,
            wash_trigger_event,
            device_state,
            device_state,
            "rejected",
            "global_fault_active",
            RUNTIME_EVENT_LOG_REJECTION);
        return operation_result_fail(ERROR_CODE_INVALID_STATE);
    }

    wash_session = system_context_private_wash_session_mutable(system_context);
    wash_execution = system_context_private_wash_execution_mutable(system_context);
    if (wash_session == 0 || wash_execution == 0) {
        return operation_result_fail(ERROR_CODE_INVALID_STATE);
    }
    if (wash_session_is_running(wash_session)) {
        project_trigger_result(system_context,
            wash_trigger_event,
            device_state,
            device_state,
            "rejected",
            "running_session_exists",
            RUNTIME_EVENT_LOG_REJECTION);
        return operation_result_fail(ERROR_CODE_INVALID_STATE);
    }

    program_snapshot_service_args = build_program_snapshot_service_args(system_context);
    result = program_snapshot_service_capture(&program_snapshot_service_args, wash_trigger_event->program_id);
    if (!result.ok) {
        reason_code = result.error_code == ERROR_CODE_RESOURCE_UNAVAILABLE
            ? "program_unavailable"
            : "program_invalid";
        project_trigger_result(system_context,
            wash_trigger_event,
            device_state,
            device_state,
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

    wash_execution_reset(wash_execution);
    wash_execution->segment_index = -1;
    wash_session->last_correlation_key[0] = '\0';
    wash_execution_service_args = build_execution_service_args(system_context);
    result = wash_execution_service_begin_next_segment(&wash_execution_service_args, &wash_execution_fact);
    if (result.ok) {
        set_device_state(system_context, DEVICE_STATE_RUNNING);
        set_latest_result(system_context, "accepted", "session_started");
    }
    return result;
}

static operation_result_t handle_stop(system_context_t system_context, const wash_trigger_event_t *wash_trigger_event)
{
    device_state_t device_state;
    const wash_session_t *wash_session;
    operation_result_t result;
    wash_execution_service_args_t wash_execution_service_args;
    wash_execution_fact_t wash_execution_fact;
    wash_session_service_args_t wash_session_service_args;
    wash_session_transition_fact_t wash_session_transition_fact;

    wash_session = system_context_private_wash_session(system_context);
    if (wash_session == 0) {
        return operation_result_fail(ERROR_CODE_INVALID_STATE);
    }
    device_state = system_context_private_device_state(system_context);
    if (device_state != DEVICE_STATE_RUNNING || !wash_session_is_running(wash_session)) {
        project_trigger_result(system_context,
            wash_trigger_event,
            device_state,
            device_state,
            "rejected",
            stop_rejection_reason(),
            RUNTIME_EVENT_LOG_REJECTION);
        return operation_result_fail(ERROR_CODE_INVALID_STATE);
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
        set_device_state(system_context, DEVICE_STATE_STOPPED);
        set_latest_result(system_context, "aborted", wash_trigger_event->signal_code);
    }
    return result;
}

static operation_result_t handle_fault(system_context_t system_context, const wash_trigger_event_t *wash_trigger_event)
{
    bool clear_requested;
    device_state_t device_state;
    const wash_session_t *wash_session;
    operation_result_t result;
    wash_execution_service_args_t wash_execution_service_args;
    wash_execution_fact_t wash_execution_fact;
    wash_session_service_args_t wash_session_service_args;
    wash_session_transition_fact_t wash_session_transition_fact;

    clear_requested = strcmp(wash_trigger_event->signal_code, "clear") == 0;
    device_state = system_context_private_device_state(system_context);
    wash_session = system_context_private_wash_session(system_context);
    if (wash_session == 0) {
        return operation_result_fail(ERROR_CODE_INVALID_STATE);
    }
    if (clear_requested) {
        if (device_state != DEVICE_STATE_EXCEPTION) {
            project_trigger_result(system_context,
                wash_trigger_event,
                device_state,
                device_state,
                "rejected",
                fault_clear_rejection_reason(),
                RUNTIME_EVENT_LOG_REJECTION);
            return operation_result_fail(ERROR_CODE_INVALID_STATE);
        }
        system_context_private_clear_global_fault(system_context);
        set_device_state(system_context, DEVICE_STATE_STOPPED);
        project_trigger_result(system_context,
            wash_trigger_event,
            DEVICE_STATE_EXCEPTION,
            DEVICE_STATE_STOPPED,
            "accepted",
            "global_fault_cleared",
            RUNTIME_EVENT_LOG_TRANSITION);
        return operation_result_ok();
    }
    if (!wash_session_is_running(wash_session)) {
        system_context_private_set_global_fault(system_context,
            wash_trigger_event->signal_code,
            wash_trigger_event->correlation_key);
        set_device_state(system_context, DEVICE_STATE_EXCEPTION);
        project_trigger_result(system_context,
            wash_trigger_event,
            device_state,
            DEVICE_STATE_EXCEPTION,
            "accepted",
            "global_fault_recorded",
            RUNTIME_EVENT_LOG_TRANSITION);
        return operation_result_ok();
    }
    system_context_private_set_global_fault(system_context,
        wash_trigger_event->signal_code,
        wash_trigger_event->correlation_key);

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
        set_device_state(system_context, DEVICE_STATE_EXCEPTION);
        set_latest_result(system_context, "aborted", wash_trigger_event->signal_code);
    }
    return result;
}

static operation_result_t handle_timeout(system_context_t system_context)
{
    operation_result_t result;
    wait_condition_t *wait_condition;
    wait_timeout_fact_t wait_timeout_fact;
    wash_execution_service_args_t wash_execution_service_args;
    wash_execution_fact_t wash_execution_fact;

    wait_condition = system_context_private_wait_condition_mutable(system_context);
    if (wait_condition == 0) {
        return operation_result_fail(ERROR_CODE_INVALID_STATE);
    }

    result = wait_timeout_service_handle_timeout(wait_condition, system_context_current_time_ms(system_context), &wait_timeout_fact);
    if (!result.ok) {
        return result;
    }

    wash_execution_service_args = build_execution_service_args(system_context);
    result = wash_execution_service_handle_timeout(&wash_execution_service_args, &wash_execution_fact);
    if (result.ok) {
        set_latest_result(system_context, wash_execution_fact.result_code, wash_execution_fact.reason_code);
        return advance_or_finish_session(system_context);
    }
    return result;
}

operation_result_t process_wash_trigger_execute(system_context_t system_context, const wash_trigger_event_t *wash_trigger_event)
{
    operation_result_t result;

    result = system_context_private_require_active(system_context);
    if (!result.ok) {
        return result;
    }
    if (wash_trigger_event == 0) {
        return operation_result_fail(ERROR_CODE_INVALID_ARGUMENT);
    }

    switch (wash_trigger_event->trigger_type) {
        case TRIGGER_TYPE_START:
            return handle_start(system_context, wash_trigger_event);
        case TRIGGER_TYPE_HOMING:
            return handle_homing(system_context);
        case TRIGGER_TYPE_STOP:
            return handle_stop(system_context, wash_trigger_event);
        case TRIGGER_TYPE_FAULT:
            return handle_fault(system_context, wash_trigger_event);
        case TRIGGER_TYPE_TIMEOUT:
            return handle_timeout(system_context);
        default:
            return operation_result_ok();
    }
}

operation_result_t process_wash_runtime_tick(system_context_t system_context)
{
    const wash_session_t *wash_session;
    operation_result_t result;
    wash_execution_service_args_t wash_execution_service_args;
    wash_execution_fact_t wash_execution_fact;

    result = system_context_private_require_active(system_context);
    if (!result.ok) {
        return result;
    }

    wash_session = system_context_private_wash_session(system_context);
    if (wash_session == 0) {
        return operation_result_fail(ERROR_CODE_INVALID_STATE);
    }
    if (!wash_session_is_running(wash_session)) {
        return operation_result_ok();
    }

    wash_execution_service_args = build_execution_service_args(system_context);
    result = wash_execution_service_tick(&wash_execution_service_args, &wash_execution_fact);
    if (!result.ok) {
        return result;
    }
    if (wash_execution_fact.changed) {
        set_latest_result(system_context, wash_execution_fact.result_code, wash_execution_fact.reason_code);
    }
    return advance_or_finish_session(system_context);
}
