#include "domain/model/wash_session.h"

#include <stdio.h>
#include <string.h>

void wash_session_reset(wash_session_t *wash_session)
{
    if (wash_session == 0) {
        return;
    }
    memset(wash_session, 0, sizeof(*wash_session));
    wash_session->session_state = SESSION_STATE_NONE;
    wash_session->latest_execution_result = EXECUTION_RESULT_NONE;
    wash_session->final_session_result = RESULT_CODE_START_FAILED;
    wash_session->last_correlation_key[0] = '\0';
}

void wash_session_create(wash_session_t *wash_session,
    const char *program_id,
    const char *program_snapshot_id,
    unsigned long started_at_ms,
    unsigned long session_sequence)
{
    if (wash_session == 0) {
        return;
    }
    wash_session_reset(wash_session);
    snprintf(wash_session->session_id,
        sizeof(wash_session->session_id),
        "session-%08lx-%08lx",
        started_at_ms & 0xfffffffful,
        session_sequence & 0xfffffffful);
    if (program_id != 0) {
        strncpy(wash_session->selected_program_id, program_id, sizeof(wash_session->selected_program_id) - 1);
    }
    if (program_snapshot_id != 0) {
        strncpy(wash_session->program_snapshot_id, program_snapshot_id, sizeof(wash_session->program_snapshot_id) - 1);
    }
    wash_session->session_state = SESSION_STATE_CREATED;
    wash_session->started_at_ms = started_at_ms;
}

void wash_session_start_running(wash_session_t *wash_session)
{
    if (wash_session == 0) {
        return;
    }
    wash_session->session_state = SESSION_STATE_RUNNING;
}

void wash_session_bind_execution(wash_session_t *wash_session, const char *execution_id, const char *stage_id)
{
    if (wash_session == 0) {
        return;
    }
    if (execution_id != 0) {
        strncpy(wash_session->current_execution_id, execution_id, sizeof(wash_session->current_execution_id) - 1);
    }
    if (stage_id != 0) {
        strncpy(wash_session->progress_stage_id, stage_id, sizeof(wash_session->progress_stage_id) - 1);
    }
}

void wash_session_record_execution_result(wash_session_t *wash_session, execution_result_t execution_result)
{
    if (wash_session == 0) {
        return;
    }
    wash_session->latest_execution_result = execution_result;
}

void wash_session_complete(wash_session_t *wash_session, result_code_t final_session_result, unsigned long ended_at_ms)
{
    if (wash_session == 0) {
        return;
    }
    wash_session->session_state = SESSION_STATE_COMPLETED;
    wash_session->final_session_result = final_session_result;
    wash_session->ended_at_ms = ended_at_ms;
}

void wash_session_abort(wash_session_t *wash_session, result_code_t final_session_result, const char *abort_reason, unsigned long ended_at_ms)
{
    if (wash_session == 0) {
        return;
    }
    wash_session->session_state = SESSION_STATE_ABORTED;
    wash_session->final_session_result = final_session_result;
    wash_session->ended_at_ms = ended_at_ms;
    if (abort_reason != 0) {
        strncpy(wash_session->abort_reason, abort_reason, sizeof(wash_session->abort_reason) - 1);
    }
}

bool wash_session_is_running(const wash_session_t *wash_session)
{
    return wash_session != 0 && wash_session->session_state == SESSION_STATE_RUNNING;
}

bool wash_session_is_terminal(const wash_session_t *wash_session)
{
    return wash_session != 0
        && (wash_session->session_state == SESSION_STATE_COMPLETED || wash_session->session_state == SESSION_STATE_ABORTED);
}
