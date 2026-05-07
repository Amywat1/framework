#include "domain/services/wash_execution_service.h"

#include <stdio.h>
#include <string.h>

#include "domain/model/chemical_action.h"
#include "shared/error_codes.h"
#include "shared/timeouts.h"

static void write_fact_field(char *target, size_t target_size, const char *value)
{
    if (target == 0 || target_size == 0) {
        return;
    }

    if (value != 0 && value[0] != '\0') {
        strncpy(target, value, target_size - 1);
        target[target_size - 1] = '\0';
        return;
    }

    strncpy(target, "none", target_size - 1);
    target[target_size - 1] = '\0';
}

static void init_execution_fact(wash_execution_fact_t *wash_execution_fact,
    const wash_execution_t *wash_execution,
    const char *previous_state,
    const char *current_state,
    const char *result_code,
    const char *reason_code,
    const char *stage_id)
{
    if (wash_execution_fact == 0) {
        return;
    }

    memset(wash_execution_fact, 0, sizeof(*wash_execution_fact));
    wash_execution_fact->changed = true;
    write_fact_field(wash_execution_fact->execution_id,
        sizeof(wash_execution_fact->execution_id),
        wash_execution != 0 ? wash_execution->execution_id : 0);
    write_fact_field(wash_execution_fact->previous_state,
        sizeof(wash_execution_fact->previous_state),
        previous_state);
    write_fact_field(wash_execution_fact->current_state,
        sizeof(wash_execution_fact->current_state),
        current_state);
    write_fact_field(wash_execution_fact->result_code,
        sizeof(wash_execution_fact->result_code),
        result_code);
    write_fact_field(wash_execution_fact->reason_code,
        sizeof(wash_execution_fact->reason_code),
        reason_code);
    write_fact_field(wash_execution_fact->stage_id,
        sizeof(wash_execution_fact->stage_id),
        stage_id);
}

static bool invalid_args(wash_execution_service_args_t *wash_execution_service_args, wash_execution_fact_t *wash_execution_fact)
{
    return wash_execution_service_args == 0
        || wash_execution_service_args->wash_execution == 0
        || wash_execution_service_args->wash_session == 0
        || wash_execution_service_args->wait_condition == 0
        || wash_execution_service_args->program_snapshot == 0
        || wash_execution_fact == 0;
}

static operation_result_t apply_stage_outputs(actuator_port_t *actuator_port, const wash_stage_t *wash_stage)
{
    int index;

    if (actuator_port == 0 || wash_stage == 0) {
        return operation_result_fail(ERROR_CODE_INVALID_ARGUMENT);
    }
    if (actuator_port->move_gantry != 0
        && actuator_port->move_gantry(actuator_port->context,
            wash_stage->gantry_motion_mode,
            wash_stage->traverse_count,
            wash_stage->stage_timeout_ms) != 0) {
        return operation_result_fail(ERROR_CODE_IO_FAILED);
    }
    if (actuator_port->control_roof_brush != 0
        && actuator_port->control_roof_brush(actuator_port->context,
            wash_stage->roof_brush_mode,
            BRUSH_TIMEOUT_MS) != 0) {
        return operation_result_fail(ERROR_CODE_IO_FAILED);
    }
    if (actuator_port->control_side_brush != 0
        && actuator_port->control_side_brush(actuator_port->context,
            wash_stage->side_brush_mode,
            BRUSH_TIMEOUT_MS) != 0) {
        return operation_result_fail(ERROR_CODE_IO_FAILED);
    }
    if (wash_stage->chemical_action_count > 0 && actuator_port->dose_chemical != 0) {
        for (index = 0; index < wash_stage->chemical_action_count; ++index) {
            const chemical_action_t *chemical_action = &wash_stage->chemical_actions[index];
            if (actuator_port->dose_chemical(actuator_port->context,
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

operation_result_t wash_execution_service_begin_next_stage(wash_execution_service_args_t *wash_execution_service_args,
    wash_execution_fact_t *wash_execution_fact)
{
    const wash_stage_t *wash_stage;
    char expected_signal[64];

    if (invalid_args(wash_execution_service_args, wash_execution_fact)
        || wash_execution_service_args->actuator_port == 0
        || wash_execution_service_args->next_execution_sequence == 0
        || wash_execution_service_args->next_wait_condition_sequence == 0) {
        return operation_result_fail(ERROR_CODE_INVALID_ARGUMENT);
    }

    if (wash_execution_service_args->wash_execution->stage_index + 1
        >= wash_execution_service_args->program_snapshot->frozen_program.stage_count) {
        wash_session_record_execution_result(wash_execution_service_args->wash_session, EXECUTION_RESULT_COMPLETED);
        init_execution_fact(wash_execution_fact,
            wash_execution_service_args->wash_execution,
            "running",
            "completed",
            "completed",
            "program_finished",
            wash_execution_service_args->wash_session->progress_stage_id);
        return operation_result_ok();
    }

    wash_stage = &wash_execution_service_args->program_snapshot->frozen_program.stages[
        wash_execution_service_args->wash_execution->stage_index + 1];
    *wash_execution_service_args->next_execution_sequence += 1;
    wash_execution_start_stage(wash_execution_service_args->wash_execution,
        wash_execution_service_args->wash_session->session_id,
        wash_execution_service_args->wash_execution->stage_index + 1,
        wash_stage->stage_id,
        wash_execution_service_args->current_time_ms,
        *wash_execution_service_args->next_execution_sequence);
    wash_session_bind_execution(wash_execution_service_args->wash_session,
        wash_execution_service_args->wash_execution->execution_id,
        wash_stage->stage_id);
    if (!apply_stage_outputs(wash_execution_service_args->actuator_port, wash_stage).ok) {
        wash_execution_abort(wash_execution_service_args->wash_execution,
            EXECUTION_RESULT_FAULTED,
            EXECUTION_END_REASON_FAULT,
            wash_execution_service_args->current_time_ms);
        wash_session_record_execution_result(wash_execution_service_args->wash_session, EXECUTION_RESULT_FAULTED);
        init_execution_fact(wash_execution_fact,
            wash_execution_service_args->wash_execution,
            "running",
            "aborted",
            "dispatch_failed",
            wash_stage->stage_id,
            wash_stage->stage_id);
        return operation_result_fail(ERROR_CODE_IO_FAILED);
    }

    snprintf(expected_signal, sizeof(expected_signal), "feedback:%s", wash_stage->stage_id);
    *wash_execution_service_args->next_wait_condition_sequence += 1;
    wait_condition_arm(wash_execution_service_args->wait_condition,
        wash_execution_service_args->wash_execution->execution_id,
        TRIGGER_TYPE_DEVICE_FEEDBACK,
        expected_signal,
        wash_execution_service_args->current_time_ms,
        wash_execution_service_args->current_time_ms + (unsigned long)wash_stage->stage_timeout_ms,
        wash_stage->sequence_no == 1 ? WAIT_TIMEOUT_POLICY_CONTINUE_SESSION : WAIT_TIMEOUT_POLICY_FINISH_EXECUTION,
        MAX_WAIT_RETRY_COUNT,
        *wash_execution_service_args->next_wait_condition_sequence);
    strncpy(wash_execution_service_args->wash_execution->wait_condition_id,
        wash_execution_service_args->wait_condition->wait_condition_id,
        sizeof(wash_execution_service_args->wash_execution->wait_condition_id) - 1);
    wash_execution_service_args->wash_execution->wait_condition_id[
        sizeof(wash_execution_service_args->wash_execution->wait_condition_id) - 1] = '\0';
    init_execution_fact(wash_execution_fact,
        wash_execution_service_args->wash_execution,
        "none",
        "running",
        "waiting",
        wash_stage->stage_id,
        wash_stage->stage_id);
    return operation_result_ok();
}

operation_result_t wash_execution_service_handle_feedback(wash_execution_service_args_t *wash_execution_service_args,
    const wash_trigger_event_t *wash_trigger_event,
    wash_execution_fact_t *wash_execution_fact)
{
    const wash_stage_t *wash_stage;

    if (invalid_args(wash_execution_service_args, wash_execution_fact) || wash_trigger_event == 0) {
        return operation_result_fail(ERROR_CODE_INVALID_ARGUMENT);
    }
    if (!wait_condition_matches(wash_execution_service_args->wait_condition,
        TRIGGER_TYPE_DEVICE_FEEDBACK,
        wash_trigger_event->signal_code)) {
        return operation_result_fail(ERROR_CODE_INVALID_STATE);
    }

    wash_stage = &wash_execution_service_args->program_snapshot->frozen_program.stages[
        wash_execution_service_args->wash_execution->stage_index];
    wait_condition_mark_satisfied(wash_execution_service_args->wait_condition);
    wash_execution_complete(wash_execution_service_args->wash_execution,
        EXECUTION_RESULT_ADVANCED,
        EXECUTION_END_REASON_NORMAL,
        wash_execution_service_args->current_time_ms);
    wash_session_record_execution_result(wash_execution_service_args->wash_session, EXECUTION_RESULT_ADVANCED);
    init_execution_fact(wash_execution_fact,
        wash_execution_service_args->wash_execution,
        "running",
        "completed",
        "advanced",
        wash_stage->stage_id,
        wash_stage->stage_id);
    return operation_result_ok();
}

operation_result_t wash_execution_service_handle_stop(wash_execution_service_args_t *wash_execution_service_args,
    const char *reason_code,
    wash_execution_fact_t *wash_execution_fact)
{
    if (invalid_args(wash_execution_service_args, wash_execution_fact)) {
        return operation_result_fail(ERROR_CODE_INVALID_ARGUMENT);
    }

    wait_condition_reset(wash_execution_service_args->wait_condition);
    wash_execution_abort(wash_execution_service_args->wash_execution,
        EXECUTION_RESULT_STOPPED,
        EXECUTION_END_REASON_STOP,
        wash_execution_service_args->current_time_ms);
    wash_session_record_execution_result(wash_execution_service_args->wash_session, EXECUTION_RESULT_STOPPED);
    init_execution_fact(wash_execution_fact,
        wash_execution_service_args->wash_execution,
        "running",
        "aborted",
        "stopped",
        reason_code,
        wash_execution_service_args->wash_execution->stage_id);
    return operation_result_ok();
}

operation_result_t wash_execution_service_handle_fault(wash_execution_service_args_t *wash_execution_service_args,
    const char *reason_code,
    wash_execution_fact_t *wash_execution_fact)
{
    if (invalid_args(wash_execution_service_args, wash_execution_fact)) {
        return operation_result_fail(ERROR_CODE_INVALID_ARGUMENT);
    }

    wait_condition_reset(wash_execution_service_args->wait_condition);
    wash_execution_abort(wash_execution_service_args->wash_execution,
        EXECUTION_RESULT_FAULTED,
        EXECUTION_END_REASON_FAULT,
        wash_execution_service_args->current_time_ms);
    wash_session_record_execution_result(wash_execution_service_args->wash_session, EXECUTION_RESULT_FAULTED);
    init_execution_fact(wash_execution_fact,
        wash_execution_service_args->wash_execution,
        "running",
        "aborted",
        "faulted",
        reason_code,
        wash_execution_service_args->wash_execution->stage_id);
    return operation_result_ok();
}

operation_result_t wash_execution_service_handle_timeout_continue(wash_execution_service_args_t *wash_execution_service_args,
    wash_execution_fact_t *wash_execution_fact)
{
    if (invalid_args(wash_execution_service_args, wash_execution_fact)) {
        return operation_result_fail(ERROR_CODE_INVALID_ARGUMENT);
    }

    wash_execution_complete(wash_execution_service_args->wash_execution,
        EXECUTION_RESULT_TIMED_OUT,
        EXECUTION_END_REASON_TIMEOUT,
        wash_execution_service_args->current_time_ms);
    wash_session_record_execution_result(wash_execution_service_args->wash_session, EXECUTION_RESULT_TIMED_OUT);
    init_execution_fact(wash_execution_fact,
        wash_execution_service_args->wash_execution,
        "running",
        "completed",
        "timed_out",
        "timeout_continue",
        wash_execution_service_args->wash_execution->stage_id);
    return operation_result_ok();
}

operation_result_t wash_execution_service_handle_timeout_finish(wash_execution_service_args_t *wash_execution_service_args,
    wash_execution_fact_t *wash_execution_fact)
{
    if (invalid_args(wash_execution_service_args, wash_execution_fact)) {
        return operation_result_fail(ERROR_CODE_INVALID_ARGUMENT);
    }

    wash_execution_complete(wash_execution_service_args->wash_execution,
        EXECUTION_RESULT_TIMED_OUT,
        EXECUTION_END_REASON_TIMEOUT,
        wash_execution_service_args->current_time_ms);
    wash_session_record_execution_result(wash_execution_service_args->wash_session, EXECUTION_RESULT_TIMED_OUT);
    init_execution_fact(wash_execution_fact,
        wash_execution_service_args->wash_execution,
        "running",
        "completed",
        "timed_out",
        "timeout_finish",
        wash_execution_service_args->wash_execution->stage_id);
    return operation_result_ok();
}

operation_result_t wash_execution_service_handle_timeout_abort(wash_execution_service_args_t *wash_execution_service_args,
    wash_execution_fact_t *wash_execution_fact)
{
    if (invalid_args(wash_execution_service_args, wash_execution_fact)) {
        return operation_result_fail(ERROR_CODE_INVALID_ARGUMENT);
    }

    wash_execution_abort(wash_execution_service_args->wash_execution,
        EXECUTION_RESULT_TIMED_OUT,
        EXECUTION_END_REASON_TIMEOUT,
        wash_execution_service_args->current_time_ms);
    wash_session_record_execution_result(wash_execution_service_args->wash_session, EXECUTION_RESULT_TIMED_OUT);
    init_execution_fact(wash_execution_fact,
        wash_execution_service_args->wash_execution,
        "running",
        "aborted",
        "timed_out",
        "timeout",
        wash_execution_service_args->wash_execution->stage_id);
    return operation_result_ok();
}
