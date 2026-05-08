#include "domain/services/wash_execution_service.h"

#include <string.h>

#include "domain/services/fault_policy_service.h"
#include "domain/services/recovery_state_machine.h"
#include "domain/services/segment_control_service.h"
#include "shared/error_codes.h"
#include "shared/timeouts.h"

static const char *lifecycle_to_string(segment_lifecycle_state_t lifecycle_state)
{
    switch (lifecycle_state) {
        case SEGMENT_LIFECYCLE_ENTERING:
            return "entering";
        case SEGMENT_LIFECYCLE_RUNNING:
            return "running";
        case SEGMENT_LIFECYCLE_EXITING:
            return "exiting";
        case SEGMENT_LIFECYCLE_COMPLETED:
            return "completed";
        case SEGMENT_LIFECYCLE_ABORTED:
            return "aborted";
        case SEGMENT_LIFECYCLE_PENDING:
        default:
            return "pending";
    }
}

static void write_field(char *target, size_t target_size, const char *value)
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

static void init_fact(wash_execution_fact_t *wash_execution_fact,
    const wash_execution_t *wash_execution,
    const char *previous_state,
    const char *current_state,
    const char *result_code,
    const char *reason_code)
{
    if (wash_execution_fact == 0) {
        return;
    }
    memset(wash_execution_fact, 0, sizeof(*wash_execution_fact));
    wash_execution_fact->changed = true;
    write_field(wash_execution_fact->execution_id, sizeof(wash_execution_fact->execution_id),
        wash_execution != 0 ? wash_execution->execution_id : 0);
    write_field(wash_execution_fact->previous_state, sizeof(wash_execution_fact->previous_state), previous_state);
    write_field(wash_execution_fact->current_state, sizeof(wash_execution_fact->current_state), current_state);
    write_field(wash_execution_fact->result_code, sizeof(wash_execution_fact->result_code), result_code);
    write_field(wash_execution_fact->reason_code, sizeof(wash_execution_fact->reason_code), reason_code);
    write_field(wash_execution_fact->segment_id, sizeof(wash_execution_fact->segment_id),
        wash_execution != 0 ? wash_execution->segment_id : 0);
}

static bool invalid_args(wash_execution_service_args_t *wash_execution_service_args, wash_execution_fact_t *wash_execution_fact)
{
    return wash_execution_service_args == 0
        || wash_execution_service_args->wash_execution == 0
        || wash_execution_service_args->wash_session == 0
        || wash_execution_service_args->wait_condition == 0
        || wash_execution_service_args->program_snapshot == 0
        || wash_execution_service_args->actuator_port == 0
        || wash_execution_service_args->sensor_port == 0
        || wash_execution_fact == 0;
}

static const wash_segment_t *current_segment(const wash_execution_service_args_t *wash_execution_service_args)
{
    if (wash_execution_service_args == 0) {
        return 0;
    }
    if (wash_execution_service_args->wash_execution->segment_index < 0
        || wash_execution_service_args->wash_execution->segment_index
            >= wash_execution_service_args->program_snapshot->frozen_program.segment_count) {
        return 0;
    }
    return &wash_execution_service_args->program_snapshot->frozen_program.segments[
        wash_execution_service_args->wash_execution->segment_index];
}

static operation_result_t read_runtime_snapshot(wash_execution_service_args_t *wash_execution_service_args,
    runtime_snapshot_t *runtime_snapshot)
{
    if (wash_execution_service_args->sensor_port->read_runtime_snapshot == 0 || runtime_snapshot == 0) {
        return operation_result_fail(ERROR_CODE_INVALID_ARGUMENT);
    }
    if (wash_execution_service_args->sensor_port->read_runtime_snapshot(
        wash_execution_service_args->sensor_port->context,
        runtime_snapshot) != 0) {
        return operation_result_fail(ERROR_CODE_IO_FAILED);
    }
    return operation_result_ok();
}

static operation_result_t apply_entering_outputs(wash_execution_service_args_t *wash_execution_service_args,
    const wash_segment_t *wash_segment)
{
    actuator_port_t *actuator_port;

    actuator_port = wash_execution_service_args->actuator_port;
    if (actuator_port->start_motion != 0
        && actuator_port->start_motion(actuator_port->context, &wash_segment->motion_plan, MOTION_TIMEOUT_MS) != 0) {
        return operation_result_fail(ERROR_CODE_IO_FAILED);
    }
    if (actuator_port->set_roof_brush_follow != 0
        && actuator_port->set_roof_brush_follow(actuator_port->context,
            wash_segment->continuous_controls.roof_brush_follow,
            BRUSH_TIMEOUT_MS) != 0) {
        return operation_result_fail(ERROR_CODE_IO_FAILED);
    }
    if (actuator_port->set_side_brush_enabled != 0
        && actuator_port->set_side_brush_enabled(actuator_port->context,
            wash_segment->continuous_controls.side_brush_enabled,
            BRUSH_TIMEOUT_MS) != 0) {
        return operation_result_fail(ERROR_CODE_IO_FAILED);
    }
    if (actuator_port->set_ro_water_enabled != 0
        && actuator_port->set_ro_water_enabled(actuator_port->context,
            wash_segment->continuous_controls.ro_water_enabled,
            CLOSE_FEEDBACK_TIMEOUT_MS) != 0) {
        return operation_result_fail(ERROR_CODE_IO_FAILED);
    }
    if (actuator_port->set_dryer_enabled != 0
        && actuator_port->set_dryer_enabled(actuator_port->context,
            wash_segment->continuous_controls.dryer_enabled,
            CLOSE_FEEDBACK_TIMEOUT_MS) != 0) {
        return operation_result_fail(ERROR_CODE_IO_FAILED);
    }
    return operation_result_ok();
}

static operation_result_t trigger_exit_actions(wash_execution_service_args_t *wash_execution_service_args,
    const wash_segment_t *wash_segment)
{
    actuator_port_t *actuator_port;
    exit_action_runtime_t *exit_runtime;

    actuator_port = wash_execution_service_args->actuator_port;
    exit_runtime = &wash_execution_service_args->wash_execution->exit_runtime;

    if (wash_segment->exit_actions.stop_roof_brush && actuator_port->stop_roof_brush != 0) {
        if (actuator_port->stop_roof_brush(actuator_port->context, BRUSH_TIMEOUT_MS) != 0) {
            return operation_result_fail(ERROR_CODE_IO_FAILED);
        }
        exit_runtime->stop_roof_brush_started = true;
    }
    if (wash_segment->exit_actions.stop_side_brush && actuator_port->stop_side_brush != 0) {
        if (actuator_port->stop_side_brush(actuator_port->context, BRUSH_TIMEOUT_MS) != 0) {
            return operation_result_fail(ERROR_CODE_IO_FAILED);
        }
        exit_runtime->stop_side_brush_started = true;
    }
    if (wash_segment->exit_actions.stop_chemical && actuator_port->stop_chemical != 0) {
        if (actuator_port->stop_chemical(actuator_port->context, CHEMICAL_TIMEOUT_MS) != 0) {
            return operation_result_fail(ERROR_CODE_IO_FAILED);
        }
        exit_runtime->stop_chemical_started = true;
    }
    if (wash_segment->exit_actions.close_ro_water && actuator_port->close_ro_water != 0) {
        if (actuator_port->close_ro_water(actuator_port->context, CLOSE_FEEDBACK_TIMEOUT_MS) != 0) {
            return operation_result_fail(ERROR_CODE_IO_FAILED);
        }
        exit_runtime->close_ro_water_started = true;
    }
    if (wash_segment->exit_actions.close_dryer && actuator_port->close_dryer != 0) {
        if (actuator_port->close_dryer(actuator_port->context, CLOSE_FEEDBACK_TIMEOUT_MS) != 0) {
            return operation_result_fail(ERROR_CODE_IO_FAILED);
        }
        exit_runtime->close_dryer_started = true;
    }
    if (wash_segment->exit_actions.roof_brush_home && actuator_port->home_roof_brush != 0) {
        if (actuator_port->home_roof_brush(actuator_port->context, HOME_FEEDBACK_TIMEOUT_MS) != 0) {
            return operation_result_fail(ERROR_CODE_IO_FAILED);
        }
        exit_runtime->roof_brush_home_started = true;
    }
    return operation_result_ok();
}

static operation_result_t execute_recovery(actuator_port_t *actuator_port)
{
    return recovery_state_machine_execute(actuator_port);
}

static operation_result_t abort_with_recovery(wash_execution_service_args_t *wash_execution_service_args,
    execution_result_t execution_result,
    execution_end_reason_t end_reason,
    const char *previous_state,
    const char *result_code,
    const char *reason_code,
    wash_execution_fact_t *wash_execution_fact)
{
    operation_result_t result;

    result = execute_recovery(wash_execution_service_args->actuator_port);
    if (!result.ok) {
        return result;
    }
    wait_condition_mark_done(wash_execution_service_args->wait_condition);
    wash_execution_abort(wash_execution_service_args->wash_execution,
        execution_result,
        end_reason,
        wash_execution_service_args->current_time_ms);
    write_field(wash_execution_service_args->wash_execution->result_code,
        sizeof(wash_execution_service_args->wash_execution->result_code),
        result_code);
    write_field(wash_execution_service_args->wash_execution->reason_code,
        sizeof(wash_execution_service_args->wash_execution->reason_code),
        reason_code);
    init_fact(wash_execution_fact,
        wash_execution_service_args->wash_execution,
        previous_state,
        "aborted",
        result_code,
        reason_code);
    return operation_result_ok();
}

static void arm_segment_timeout(wash_execution_service_args_t *wash_execution_service_args, int timeout_ms, wait_timeout_policy_t wait_timeout_policy)
{
    *wash_execution_service_args->next_wait_condition_sequence += 1;
    wait_condition_arm(wash_execution_service_args->wait_condition,
        wash_execution_service_args->wash_execution->execution_id,
        wait_timeout_policy == WAIT_TIMEOUT_POLICY_SEGMENT ? "segment_timeout" : "exit_timeout",
        wash_execution_service_args->current_time_ms,
        wash_execution_service_args->current_time_ms + (unsigned long)timeout_ms,
        wait_timeout_policy,
        *wash_execution_service_args->next_wait_condition_sequence);
}

static operation_result_t begin_exit(wash_execution_service_args_t *wash_execution_service_args,
    const wash_segment_t *wash_segment,
    wash_execution_fact_t *wash_execution_fact)
{
    int index;
    operation_result_t result;

    result = trigger_exit_actions(wash_execution_service_args, wash_segment);
    if (!result.ok) {
        return result;
    }
    wash_execution_begin_exit(wash_execution_service_args->wash_execution, wash_execution_service_args->current_time_ms);
    arm_segment_timeout(wash_execution_service_args, wash_segment->exit_timeout_ms, WAIT_TIMEOUT_POLICY_EXIT);
    for (index = 0; index < wash_segment->conditional_control_count; ++index) {
        if (!wash_segment->conditional_controls[index].active_during_exit) {
            wash_execution_service_args->wash_execution->active_conditional_controls[index] = false;
        }
    }
    init_fact(wash_execution_fact,
        wash_execution_service_args->wash_execution,
        "running",
        "exiting",
        "exiting",
        "segment_complete");
    return operation_result_ok();
}

static bool feedback_or_fallback_done(bool feedback_available,
    bool feedback_done,
    unsigned long elapsed_ms,
    unsigned long fallback_timeout_ms)
{
    if (feedback_available) {
        return feedback_done;
    }
    return elapsed_ms >= fallback_timeout_ms;
}

static bool all_exit_actions_done(const wash_segment_t *wash_segment,
    wash_execution_t *wash_execution,
    const runtime_snapshot_t *runtime_snapshot,
    unsigned long current_time_ms)
{
    unsigned long exit_elapsed_ms;

    exit_elapsed_ms = current_time_ms - wash_execution->exit_started_at_ms;

    if (wash_segment->exit_actions.stop_roof_brush) {
        wash_execution->exit_runtime.stop_roof_brush_done = feedback_or_fallback_done(
            runtime_snapshot->actuator_feedback.roof_brush_stopped_feedback_available,
            runtime_snapshot->actuator_feedback.roof_brush_stopped,
            exit_elapsed_ms,
            CLOSE_FEEDBACK_TIMEOUT_MS);
    }
    if (wash_segment->exit_actions.stop_side_brush) {
        wash_execution->exit_runtime.stop_side_brush_done = feedback_or_fallback_done(
            runtime_snapshot->actuator_feedback.side_brush_stopped_feedback_available,
            runtime_snapshot->actuator_feedback.side_brush_stopped,
            exit_elapsed_ms,
            CLOSE_FEEDBACK_TIMEOUT_MS);
    }
    if (wash_segment->exit_actions.stop_chemical) {
        wash_execution->exit_runtime.stop_chemical_done = feedback_or_fallback_done(
            runtime_snapshot->actuator_feedback.chemical_closed_feedback_available,
            runtime_snapshot->actuator_feedback.chemical_closed,
            exit_elapsed_ms,
            CLOSE_FEEDBACK_TIMEOUT_MS);
    }
    if (wash_segment->exit_actions.close_ro_water) {
        wash_execution->exit_runtime.close_ro_water_done = feedback_or_fallback_done(
            runtime_snapshot->actuator_feedback.ro_water_closed_feedback_available,
            runtime_snapshot->actuator_feedback.ro_water_closed,
            exit_elapsed_ms,
            CLOSE_FEEDBACK_TIMEOUT_MS);
    }
    if (wash_segment->exit_actions.close_dryer) {
        wash_execution->exit_runtime.close_dryer_done = feedback_or_fallback_done(
            runtime_snapshot->actuator_feedback.dryer_closed_feedback_available,
            runtime_snapshot->actuator_feedback.dryer_closed,
            exit_elapsed_ms,
            CLOSE_FEEDBACK_TIMEOUT_MS);
    }
    if (wash_segment->exit_actions.roof_brush_home) {
        wash_execution->exit_runtime.roof_brush_home_done =
            runtime_snapshot->actuator_feedback.roof_brush_home_feedback_available
            && runtime_snapshot->actuator_feedback.roof_brush_home_reached;
    }

    return (!wash_segment->exit_actions.stop_roof_brush || wash_execution->exit_runtime.stop_roof_brush_done)
        && (!wash_segment->exit_actions.stop_side_brush || wash_execution->exit_runtime.stop_side_brush_done)
        && (!wash_segment->exit_actions.stop_chemical || wash_execution->exit_runtime.stop_chemical_done)
        && (!wash_segment->exit_actions.close_ro_water || wash_execution->exit_runtime.close_ro_water_done)
        && (!wash_segment->exit_actions.close_dryer || wash_execution->exit_runtime.close_dryer_done)
        && (!wash_segment->exit_actions.roof_brush_home || wash_execution->exit_runtime.roof_brush_home_done);
}

operation_result_t wash_execution_service_begin_next_segment(wash_execution_service_args_t *wash_execution_service_args,
    wash_execution_fact_t *wash_execution_fact)
{
    const wash_segment_t *wash_segment;

    if (invalid_args(wash_execution_service_args, wash_execution_fact)
        || wash_execution_service_args->next_execution_sequence == 0
        || wash_execution_service_args->next_wait_condition_sequence == 0) {
        return operation_result_fail(ERROR_CODE_INVALID_ARGUMENT);
    }

    if (wash_execution_service_args->wash_execution->segment_index + 1
        >= wash_execution_service_args->program_snapshot->frozen_program.segment_count) {
        return operation_result_fail(ERROR_CODE_INVALID_STATE);
    }

    wash_segment = &wash_execution_service_args->program_snapshot->frozen_program.segments[
        wash_execution_service_args->wash_execution->segment_index + 1];
    *wash_execution_service_args->next_execution_sequence += 1;
    wash_execution_start_segment(wash_execution_service_args->wash_execution,
        wash_execution_service_args->wash_session->session_id,
        wash_execution_service_args->wash_execution->segment_index + 1,
        wash_segment->segment_id,
        wash_execution_service_args->current_time_ms,
        *wash_execution_service_args->next_execution_sequence);
    wash_session_bind_execution(wash_execution_service_args->wash_session,
        wash_execution_service_args->wash_execution->execution_id,
        wash_segment->segment_id);
    init_fact(wash_execution_fact,
        wash_execution_service_args->wash_execution,
        "pending",
        "entering",
        "entering",
        "segment_loaded");
    return operation_result_ok();
}

operation_result_t wash_execution_service_tick(wash_execution_service_args_t *wash_execution_service_args,
    wash_execution_fact_t *wash_execution_fact)
{
    const wash_segment_t *wash_segment;
    runtime_snapshot_t runtime_snapshot;
    segment_control_evaluation_t segment_control_evaluation;
    operation_result_t result;
    fault_policy_decision_t fault_policy_decision;
    int index;

    if (invalid_args(wash_execution_service_args, wash_execution_fact)) {
        return operation_result_fail(ERROR_CODE_INVALID_ARGUMENT);
    }

    memset(wash_execution_fact, 0, sizeof(*wash_execution_fact));
    wash_segment = current_segment(wash_execution_service_args);
    if (wash_segment == 0 || wash_execution_service_args->wash_execution->execution_state != EXECUTION_STATE_RUNNING) {
        return operation_result_ok();
    }

    if (wash_execution_service_args->wash_execution->lifecycle_state == SEGMENT_LIFECYCLE_ENTERING) {
        result = apply_entering_outputs(wash_execution_service_args, wash_segment);
        if (!result.ok) {
            return result;
        }
        wash_execution_mark_running(wash_execution_service_args->wash_execution);
        arm_segment_timeout(wash_execution_service_args, wash_segment->segment_timeout_ms, WAIT_TIMEOUT_POLICY_SEGMENT);
        init_fact(wash_execution_fact,
            wash_execution_service_args->wash_execution,
            "entering",
            "running",
            "running",
            "segment_running");
        return operation_result_ok();
    }

    result = read_runtime_snapshot(wash_execution_service_args, &runtime_snapshot);
    if (!result.ok) {
        return abort_with_recovery(wash_execution_service_args,
            EXECUTION_RESULT_FAULTED,
            EXECUTION_END_REASON_FAULT,
            lifecycle_to_string(wash_execution_service_args->wash_execution->lifecycle_state),
            "aborted",
            "runtime_snapshot_unavailable",
            wash_execution_fact);
    }

    if (wash_execution_service_args->wash_execution->lifecycle_state == SEGMENT_LIFECYCLE_RUNNING) {
        result = segment_control_service_evaluate(wash_segment,
            wash_execution_service_args->wash_execution,
            &runtime_snapshot,
            &segment_control_evaluation);
        if (!result.ok) {
            return result;
        }
        if (segment_control_evaluation.position_lost) {
            fault_policy_service_resolve(wash_segment->exception_policy.on_position_lost,
                false,
                &fault_policy_decision);
            if (fault_policy_decision.enter_exit) {
                return begin_exit(wash_execution_service_args, wash_segment, wash_execution_fact);
            }
            result = execute_recovery(wash_execution_service_args->actuator_port);
            if (!result.ok) {
                return result;
            }
            wait_condition_mark_done(wash_execution_service_args->wait_condition);
            wash_execution_abort(wash_execution_service_args->wash_execution,
                EXECUTION_RESULT_POSITION_LOST,
                EXECUTION_END_REASON_FAULT,
                wash_execution_service_args->current_time_ms);
            write_field(wash_execution_service_args->wash_execution->result_code,
                sizeof(wash_execution_service_args->wash_execution->result_code),
                "aborted");
            write_field(wash_execution_service_args->wash_execution->reason_code,
                sizeof(wash_execution_service_args->wash_execution->reason_code),
                "position_lost");
            init_fact(wash_execution_fact,
                wash_execution_service_args->wash_execution,
                "running",
                "aborted",
                "aborted",
                "position_lost");
            return operation_result_ok();
        }
        if (segment_control_evaluation.follow_lost) {
            fault_policy_service_resolve(wash_segment->exception_policy.on_follow_lost,
                false,
                &fault_policy_decision);
            if (fault_policy_decision.enter_exit) {
                return begin_exit(wash_execution_service_args, wash_segment, wash_execution_fact);
            }
            result = execute_recovery(wash_execution_service_args->actuator_port);
            if (!result.ok) {
                return result;
            }
            wait_condition_mark_done(wash_execution_service_args->wait_condition);
            wash_execution_abort(wash_execution_service_args->wash_execution,
                EXECUTION_RESULT_FOLLOW_LOST,
                EXECUTION_END_REASON_FAULT,
                wash_execution_service_args->current_time_ms);
            write_field(wash_execution_service_args->wash_execution->result_code,
                sizeof(wash_execution_service_args->wash_execution->result_code),
                "aborted");
            write_field(wash_execution_service_args->wash_execution->reason_code,
                sizeof(wash_execution_service_args->wash_execution->reason_code),
                "follow_lost");
            init_fact(wash_execution_fact,
                wash_execution_service_args->wash_execution,
                "running",
                "aborted",
                "aborted",
                "follow_lost");
            return operation_result_ok();
        }

        if (segment_control_evaluation.segment_complete) {
            wait_condition_mark_done(wash_execution_service_args->wait_condition);
            return begin_exit(wash_execution_service_args, wash_segment, wash_execution_fact);
        }

        for (index = 0; index < wash_segment->conditional_control_count; ++index) {
            if (segment_control_evaluation.start_chemical[index]
                && wash_execution_service_args->actuator_port->set_chemical_enabled != 0) {
                if (wash_execution_service_args->actuator_port->set_chemical_enabled(
                    wash_execution_service_args->actuator_port->context,
                    true,
                    CHEMICAL_TIMEOUT_MS) != 0) {
                    return operation_result_fail(ERROR_CODE_IO_FAILED);
                }
                wash_execution_service_args->wash_execution->active_conditional_controls[index] = true;
            }
            if (segment_control_evaluation.stop_chemical[index]
                && wash_execution_service_args->actuator_port->set_chemical_enabled != 0) {
                if (wash_execution_service_args->actuator_port->set_chemical_enabled(
                    wash_execution_service_args->actuator_port->context,
                    false,
                    CHEMICAL_TIMEOUT_MS) != 0) {
                    return operation_result_fail(ERROR_CODE_IO_FAILED);
                }
                wash_execution_service_args->wash_execution->active_conditional_controls[index] = false;
            }
        }

        return operation_result_ok();
    }

    if (wash_execution_service_args->wash_execution->lifecycle_state == SEGMENT_LIFECYCLE_EXITING) {
        if (all_exit_actions_done(wash_segment,
            wash_execution_service_args->wash_execution,
            &runtime_snapshot,
            wash_execution_service_args->current_time_ms)) {
            wait_condition_mark_done(wash_execution_service_args->wait_condition);
            wash_execution_complete(wash_execution_service_args->wash_execution,
                EXECUTION_RESULT_SEGMENT_COMPLETED,
                EXECUTION_END_REASON_NORMAL,
                wash_execution_service_args->current_time_ms);
            write_field(wash_execution_service_args->wash_execution->result_code,
                sizeof(wash_execution_service_args->wash_execution->result_code),
                "completed");
            write_field(wash_execution_service_args->wash_execution->reason_code,
                sizeof(wash_execution_service_args->wash_execution->reason_code),
                "exit_complete");
            init_fact(wash_execution_fact,
                wash_execution_service_args->wash_execution,
                "exiting",
                "completed",
                "completed",
                "exit_complete");
        }
    }
    return operation_result_ok();
}

operation_result_t wash_execution_service_handle_stop(wash_execution_service_args_t *wash_execution_service_args,
    const char *reason_code,
    wash_execution_fact_t *wash_execution_fact)
{
    if (invalid_args(wash_execution_service_args, wash_execution_fact)) {
        return operation_result_fail(ERROR_CODE_INVALID_ARGUMENT);
    }

    {
        operation_result_t result = execute_recovery(wash_execution_service_args->actuator_port);
        if (!result.ok) {
            return result;
        }
    }
    wait_condition_mark_done(wash_execution_service_args->wait_condition);
    wash_execution_abort(wash_execution_service_args->wash_execution,
        EXECUTION_RESULT_STOPPED,
        EXECUTION_END_REASON_STOP,
        wash_execution_service_args->current_time_ms);
    write_field(wash_execution_service_args->wash_execution->result_code,
        sizeof(wash_execution_service_args->wash_execution->result_code),
        "stopped");
    write_field(wash_execution_service_args->wash_execution->reason_code,
        sizeof(wash_execution_service_args->wash_execution->reason_code),
        reason_code);
    init_fact(wash_execution_fact,
        wash_execution_service_args->wash_execution,
        lifecycle_to_string(SEGMENT_LIFECYCLE_RUNNING),
        "aborted",
        "stopped",
        reason_code);
    return operation_result_ok();
}

operation_result_t wash_execution_service_handle_fault(wash_execution_service_args_t *wash_execution_service_args,
    const char *reason_code,
    wash_execution_fact_t *wash_execution_fact)
{
    if (invalid_args(wash_execution_service_args, wash_execution_fact)) {
        return operation_result_fail(ERROR_CODE_INVALID_ARGUMENT);
    }

    {
        operation_result_t result = execute_recovery(wash_execution_service_args->actuator_port);
        if (!result.ok) {
            return result;
        }
    }
    wait_condition_mark_done(wash_execution_service_args->wait_condition);
    wash_execution_abort(wash_execution_service_args->wash_execution,
        EXECUTION_RESULT_FAULTED,
        EXECUTION_END_REASON_FAULT,
        wash_execution_service_args->current_time_ms);
    write_field(wash_execution_service_args->wash_execution->result_code,
        sizeof(wash_execution_service_args->wash_execution->result_code),
        "faulted");
    write_field(wash_execution_service_args->wash_execution->reason_code,
        sizeof(wash_execution_service_args->wash_execution->reason_code),
        reason_code);
    init_fact(wash_execution_fact,
        wash_execution_service_args->wash_execution,
        lifecycle_to_string(wash_execution_service_args->wash_execution->lifecycle_state),
        "aborted",
        "faulted",
        reason_code);
    return operation_result_ok();
}

operation_result_t wash_execution_service_handle_timeout(wash_execution_service_args_t *wash_execution_service_args,
    wash_execution_fact_t *wash_execution_fact)
{
    const wash_segment_t *wash_segment;
    fault_policy_decision_t fault_policy_decision;
    exception_strategy_t exception_strategy;
    bool currently_exiting;

    if (invalid_args(wash_execution_service_args, wash_execution_fact)) {
        return operation_result_fail(ERROR_CODE_INVALID_ARGUMENT);
    }

    wash_segment = current_segment(wash_execution_service_args);
    if (wash_segment == 0) {
        return operation_result_fail(ERROR_CODE_INVALID_STATE);
    }
    currently_exiting = wash_execution_service_args->wash_execution->lifecycle_state == SEGMENT_LIFECYCLE_EXITING;
    exception_strategy = currently_exiting
        ? wash_segment->exception_policy.on_exit_timeout
        : wash_segment->exception_policy.on_segment_timeout;
    fault_policy_service_resolve(exception_strategy, currently_exiting, &fault_policy_decision);
    if (fault_policy_decision.enter_exit && !currently_exiting) {
        return begin_exit(wash_execution_service_args, wash_segment, wash_execution_fact);
    }

    {
        operation_result_t result = execute_recovery(wash_execution_service_args->actuator_port);
        if (!result.ok) {
            return result;
        }
    }
    wait_condition_mark_done(wash_execution_service_args->wait_condition);
    wash_execution_abort(wash_execution_service_args->wash_execution,
        currently_exiting ? EXECUTION_RESULT_EXIT_TIMEOUT : EXECUTION_RESULT_SEGMENT_TIMEOUT,
        EXECUTION_END_REASON_TIMEOUT,
        wash_execution_service_args->current_time_ms);
    write_field(wash_execution_service_args->wash_execution->result_code,
        sizeof(wash_execution_service_args->wash_execution->result_code),
        currently_exiting ? "exit_timeout" : "segment_timeout");
    write_field(wash_execution_service_args->wash_execution->reason_code,
        sizeof(wash_execution_service_args->wash_execution->reason_code),
        currently_exiting ? "exit_timeout" : "segment_timeout");
    init_fact(wash_execution_fact,
        wash_execution_service_args->wash_execution,
        currently_exiting ? "exiting" : "running",
        "aborted",
        currently_exiting ? "exit_timeout" : "segment_timeout",
        currently_exiting ? "exit_timeout" : "segment_timeout");
    return operation_result_ok();
}
