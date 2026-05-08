#include "domain/services/wash_session_state_machine.h"

#include <string.h>

#include "shared/error_codes.h"

static const char *session_state_to_string(session_state_t session_state)
{
    switch (session_state) {
        case SESSION_STATE_CREATED:
            return "created";
        case SESSION_STATE_RUNNING:
            return "running";
        case SESSION_STATE_COMPLETED:
            return "completed";
        case SESSION_STATE_ABORTED:
            return "aborted";
        default:
            return "none";
    }
}

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

static void init_transition_fact(wash_session_transition_fact_t *wash_session_transition_fact,
    const wash_session_t *wash_session,
    const char *previous_state,
    const char *current_state,
    const char *result_code,
    const char *reason_code)
{
    if (wash_session_transition_fact == 0) {
        return;
    }

    memset(wash_session_transition_fact, 0, sizeof(*wash_session_transition_fact));
    wash_session_transition_fact->changed = true;
    write_fact_field(wash_session_transition_fact->session_id,
        sizeof(wash_session_transition_fact->session_id),
        wash_session != 0 ? wash_session->session_id : 0);
    write_fact_field(wash_session_transition_fact->previous_state,
        sizeof(wash_session_transition_fact->previous_state),
        previous_state);
    write_fact_field(wash_session_transition_fact->current_state,
        sizeof(wash_session_transition_fact->current_state),
        current_state);
    write_fact_field(wash_session_transition_fact->result_code,
        sizeof(wash_session_transition_fact->result_code),
        result_code);
    write_fact_field(wash_session_transition_fact->reason_code,
        sizeof(wash_session_transition_fact->reason_code),
        reason_code);
}

static bool invalid_args(wash_session_service_args_t *wash_session_service_args,
    wash_session_transition_fact_t *wash_session_transition_fact)
{
    return wash_session_service_args == 0
        || wash_session_service_args->wash_session == 0
        || wash_session_transition_fact == 0;
}

operation_result_t wash_session_state_machine_start(wash_session_service_args_t *wash_session_service_args,
    const char *program_id,
    wash_session_transition_fact_t *wash_session_transition_fact)
{
    const char *previous_state;

    if (invalid_args(wash_session_service_args, wash_session_transition_fact)
        || program_id == 0
        || wash_session_service_args->program_snapshot == 0
        || wash_session_service_args->next_session_sequence == 0) {
        return operation_result_fail(ERROR_CODE_INVALID_ARGUMENT);
    }
    if (wash_session_is_running(wash_session_service_args->wash_session)) {
        return operation_result_fail(ERROR_CODE_INVALID_STATE);
    }

    previous_state = session_state_to_string(wash_session_service_args->wash_session->session_state);
    *wash_session_service_args->next_session_sequence += 1;
    wash_session_create(wash_session_service_args->wash_session,
        program_id,
        wash_session_service_args->program_snapshot->program_snapshot_id,
        wash_session_service_args->current_time_ms,
        *wash_session_service_args->next_session_sequence);
    wash_session_start_running(wash_session_service_args->wash_session);
    init_transition_fact(wash_session_transition_fact,
        wash_session_service_args->wash_session,
        previous_state,
        session_state_to_string(wash_session_service_args->wash_session->session_state),
        "accepted",
        "session_started");
    return operation_result_ok();
}

operation_result_t wash_session_state_machine_complete(wash_session_service_args_t *wash_session_service_args,
    result_code_t result_code,
    wash_session_transition_fact_t *wash_session_transition_fact)
{
    const char *previous_state;

    if (invalid_args(wash_session_service_args, wash_session_transition_fact)) {
        return operation_result_fail(ERROR_CODE_INVALID_ARGUMENT);
    }

    previous_state = session_state_to_string(wash_session_service_args->wash_session->session_state);
    wash_session_complete(wash_session_service_args->wash_session,
        result_code,
        wash_session_service_args->current_time_ms);
    init_transition_fact(wash_session_transition_fact,
        wash_session_service_args->wash_session,
        previous_state,
        session_state_to_string(wash_session_service_args->wash_session->session_state),
        "completed",
        "program_finished");
    return operation_result_ok();
}

operation_result_t wash_session_state_machine_abort(wash_session_service_args_t *wash_session_service_args,
    result_code_t result_code,
    const char *reason_code,
    wash_session_transition_fact_t *wash_session_transition_fact)
{
    const char *previous_state;

    if (invalid_args(wash_session_service_args, wash_session_transition_fact)) {
        return operation_result_fail(ERROR_CODE_INVALID_ARGUMENT);
    }

    previous_state = session_state_to_string(wash_session_service_args->wash_session->session_state);
    wash_session_abort(wash_session_service_args->wash_session,
        result_code,
        reason_code,
        wash_session_service_args->current_time_ms);
    init_transition_fact(wash_session_transition_fact,
        wash_session_service_args->wash_session,
        previous_state,
        session_state_to_string(wash_session_service_args->wash_session->session_state),
        "aborted",
        reason_code);
    return operation_result_ok();
}
