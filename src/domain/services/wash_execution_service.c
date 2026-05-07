#include "domain/services/wash_execution_service.h"

#include <stdio.h>
#include <string.h>

#include "application/coordinators/runtime_event_recorder.h"
#include "domain/model/chemical_action.h"
#include "shared/error_codes.h"
#include "shared/timeouts.h"

static void log_execution_transition(system_context_t *system_context,
    trigger_type_t trigger_type,
    const char *previous_state,
    const char *current_state,
    const char *result_code,
    const char *reason_code)
{
    if (system_context == 0) {
        return;
    }
    runtime_event_recorder_record(system_context,
        TRANSITION_ENTITY_EXECUTION,
        system_context->wash_execution.execution_id,
        trigger_type,
        previous_state,
        current_state,
        result_code,
        reason_code,
        RUNTIME_EVENT_LOG_TRANSITION);
}

static void log_wait_retry(system_context_t *system_context, const char *reason_code)
{
    if (system_context == 0) {
        return;
    }
    log_execution_transition(system_context,
        TRIGGER_TYPE_TIMEOUT,
        "running",
        "running",
        "waiting",
        reason_code);
}

static operation_result_t apply_stage_outputs(system_context_t *system_context, const wash_stage_t *wash_stage)
{
    int index;

    if (system_context == 0 || wash_stage == 0) {
        return operation_result_fail(ERROR_CODE_INVALID_ARGUMENT);
    }
    if (system_context->actuator_port.move_gantry != 0
        && system_context->actuator_port.move_gantry(system_context->actuator_port.context,
            wash_stage->gantry_motion_mode,
            wash_stage->traverse_count,
            wash_stage->stage_timeout_ms) != 0) {
        return operation_result_fail(ERROR_CODE_IO_FAILED);
    }
    if (system_context->actuator_port.control_roof_brush != 0
        && system_context->actuator_port.control_roof_brush(system_context->actuator_port.context,
            wash_stage->roof_brush_mode,
            BRUSH_TIMEOUT_MS) != 0) {
        return operation_result_fail(ERROR_CODE_IO_FAILED);
    }
    if (system_context->actuator_port.control_side_brush != 0
        && system_context->actuator_port.control_side_brush(system_context->actuator_port.context,
            wash_stage->side_brush_mode,
            BRUSH_TIMEOUT_MS) != 0) {
        return operation_result_fail(ERROR_CODE_IO_FAILED);
    }
    if (wash_stage->chemical_action_count > 0 && system_context->actuator_port.dose_chemical != 0) {
        for (index = 0; index < wash_stage->chemical_action_count; ++index) {
            const chemical_action_t *chemical_action = &wash_stage->chemical_actions[index];
            if (system_context->actuator_port.dose_chemical(system_context->actuator_port.context,
                chemical_action->channel_id,
                chemical_action->duration_ms,
                chemical_action->retry_limit,
                CHEMICAL_TIMEOUT_MS) != 0) {
                return operation_result_fail(ERROR_CODE_IO_FAILED);
            }
        }
    }
    return operation_result_ok();
}

operation_result_t wash_execution_service_begin_next_stage(system_context_t *system_context)
{
    const wash_stage_t *wash_stage;
    char expected_signal[64];

    if (system_context == 0) {
        return operation_result_fail(ERROR_CODE_INVALID_ARGUMENT);
    }

    if (system_context->wash_execution.stage_index + 1 >= system_context->program_snapshot.frozen_program.stage_count) {
        wash_session_record_execution_result(&system_context->wash_session, EXECUTION_RESULT_COMPLETED);
        return operation_result_ok();
    }

    wash_stage = &system_context->program_snapshot.frozen_program.stages[system_context->wash_execution.stage_index + 1];
    system_context->next_execution_sequence += 1;
    wash_execution_start_stage(&system_context->wash_execution,
        system_context->wash_session.session_id,
        system_context->wash_execution.stage_index + 1,
        wash_stage->stage_id,
        system_context->current_time_ms,
        system_context->next_execution_sequence);
    wash_session_bind_execution(&system_context->wash_session,
        system_context->wash_execution.execution_id,
        wash_stage->stage_id);
    if (!apply_stage_outputs(system_context, wash_stage).ok) {
        wash_execution_abort(&system_context->wash_execution,
            EXECUTION_RESULT_FAULTED,
            EXECUTION_END_REASON_FAULT,
            system_context->current_time_ms);
        wash_session_record_execution_result(&system_context->wash_session, EXECUTION_RESULT_FAULTED);
        log_execution_transition(system_context, TRIGGER_TYPE_START, "running", "aborted", "dispatch_failed", wash_stage->stage_id);
        return operation_result_fail(ERROR_CODE_IO_FAILED);
    }
    snprintf(expected_signal, sizeof(expected_signal), "feedback:%s", wash_stage->stage_id);
    system_context->next_wait_condition_sequence += 1;
    wait_condition_arm(&system_context->wait_condition,
        system_context->wash_execution.execution_id,
        TRIGGER_TYPE_DEVICE_FEEDBACK,
        expected_signal,
        system_context->current_time_ms,
        system_context->current_time_ms + (unsigned long)wash_stage->stage_timeout_ms,
        wash_stage->sequence_no == 1 ? WAIT_TIMEOUT_POLICY_CONTINUE_SESSION : WAIT_TIMEOUT_POLICY_FINISH_EXECUTION,
        MAX_WAIT_RETRY_COUNT,
        system_context->next_wait_condition_sequence);
    strncpy(system_context->wash_execution.wait_condition_id,
        system_context->wait_condition.wait_condition_id,
        sizeof(system_context->wash_execution.wait_condition_id) - 1);
    log_execution_transition(system_context, TRIGGER_TYPE_START, "none", "running", "waiting", wash_stage->stage_id);
    return operation_result_ok();
}

operation_result_t wash_execution_service_handle_feedback(system_context_t *system_context, const wash_trigger_event_t *wash_trigger_event)
{
    const wash_stage_t *wash_stage;

    if (system_context == 0 || wash_trigger_event == 0) {
        return operation_result_fail(ERROR_CODE_INVALID_ARGUMENT);
    }
    if (!wait_condition_matches(&system_context->wait_condition, TRIGGER_TYPE_DEVICE_FEEDBACK, wash_trigger_event->signal_code)) {
        return operation_result_fail(ERROR_CODE_INVALID_STATE);
    }

    wash_stage = &system_context->program_snapshot.frozen_program.stages[system_context->wash_execution.stage_index];

    wait_condition_mark_satisfied(&system_context->wait_condition);
    wash_execution_complete(&system_context->wash_execution,
        EXECUTION_RESULT_ADVANCED,
        EXECUTION_END_REASON_NORMAL,
        system_context->current_time_ms);
    wash_session_record_execution_result(&system_context->wash_session, EXECUTION_RESULT_ADVANCED);
    log_execution_transition(system_context, TRIGGER_TYPE_DEVICE_FEEDBACK, "running", "completed", "advanced", wash_stage->stage_id);
    return operation_result_ok();
}

operation_result_t wash_execution_service_handle_stop(system_context_t *system_context, const char *reason_code)
{
    if (system_context == 0) {
        return operation_result_fail(ERROR_CODE_INVALID_ARGUMENT);
    }
    wait_condition_reset(&system_context->wait_condition);
    wash_execution_abort(&system_context->wash_execution,
        EXECUTION_RESULT_STOPPED,
        EXECUTION_END_REASON_STOP,
        system_context->current_time_ms);
    wash_session_record_execution_result(&system_context->wash_session, EXECUTION_RESULT_STOPPED);
    log_execution_transition(system_context, TRIGGER_TYPE_STOP, "running", "aborted", "stopped", reason_code);
    return operation_result_ok();
}

operation_result_t wash_execution_service_handle_fault(system_context_t *system_context, const char *reason_code)
{
    if (system_context == 0) {
        return operation_result_fail(ERROR_CODE_INVALID_ARGUMENT);
    }
    wait_condition_reset(&system_context->wait_condition);
    wash_execution_abort(&system_context->wash_execution,
        EXECUTION_RESULT_FAULTED,
        EXECUTION_END_REASON_FAULT,
        system_context->current_time_ms);
    wash_session_record_execution_result(&system_context->wash_session, EXECUTION_RESULT_FAULTED);
    log_execution_transition(system_context, TRIGGER_TYPE_FAULT, "running", "aborted", "faulted", reason_code);
    return operation_result_ok();
}

bool wash_execution_service_is_waiting(const system_context_t *system_context)
{
    return system_context != 0 && system_context->wait_condition.active;
}

void wash_execution_service_log_retry(system_context_t *system_context, const char *reason_code)
{
    log_wait_retry(system_context, reason_code);
}

operation_result_t wash_execution_service_handle_timeout_continue(system_context_t *system_context)
{
    if (system_context == 0) {
        return operation_result_fail(ERROR_CODE_INVALID_ARGUMENT);
    }
    wash_execution_complete(&system_context->wash_execution,
        EXECUTION_RESULT_TIMED_OUT,
        EXECUTION_END_REASON_TIMEOUT,
        system_context->current_time_ms);
    wash_session_record_execution_result(&system_context->wash_session, EXECUTION_RESULT_TIMED_OUT);
    log_execution_transition(system_context, TRIGGER_TYPE_TIMEOUT, "running", "completed", "timed_out", "timeout_continue");
    return operation_result_ok();
}

operation_result_t wash_execution_service_handle_timeout_finish(system_context_t *system_context)
{
    if (system_context == 0) {
        return operation_result_fail(ERROR_CODE_INVALID_ARGUMENT);
    }
    wash_execution_complete(&system_context->wash_execution,
        EXECUTION_RESULT_TIMED_OUT,
        EXECUTION_END_REASON_TIMEOUT,
        system_context->current_time_ms);
    wash_session_record_execution_result(&system_context->wash_session, EXECUTION_RESULT_TIMED_OUT);
    log_execution_transition(system_context, TRIGGER_TYPE_TIMEOUT, "running", "completed", "timed_out", "timeout_finish");
    return operation_result_ok();
}

operation_result_t wash_execution_service_handle_timeout_abort(system_context_t *system_context)
{
    if (system_context == 0) {
        return operation_result_fail(ERROR_CODE_INVALID_ARGUMENT);
    }
    wash_execution_abort(&system_context->wash_execution,
        EXECUTION_RESULT_TIMED_OUT,
        EXECUTION_END_REASON_TIMEOUT,
        system_context->current_time_ms);
    wash_session_record_execution_result(&system_context->wash_session, EXECUTION_RESULT_TIMED_OUT);
    log_execution_transition(system_context, TRIGGER_TYPE_TIMEOUT, "running", "aborted", "timed_out", "timeout");
    return operation_result_ok();
}
