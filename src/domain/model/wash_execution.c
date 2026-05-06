#include "domain/model/wash_execution.h"

#include <stdio.h>
#include <string.h>

void wash_execution_reset(wash_execution_t *wash_execution)
{
    if (wash_execution == 0) {
        return;
    }
    memset(wash_execution, 0, sizeof(*wash_execution));
    wash_execution->execution_state = EXECUTION_STATE_NONE;
    wash_execution->execution_result = EXECUTION_RESULT_NONE;
    wash_execution->end_reason = EXECUTION_END_REASON_NONE;
    wash_execution->stage_index = -1;
}

void wash_execution_start_stage(wash_execution_t *wash_execution,
    const char *session_id,
    int stage_index,
    const char *stage_id,
    unsigned long started_at_ms,
    unsigned long execution_sequence)
{
    if (wash_execution == 0) {
        return;
    }
    wash_execution_reset(wash_execution);
    snprintf(wash_execution->execution_id,
        sizeof(wash_execution->execution_id),
        "exec-%08lx-%08lx",
        started_at_ms & 0xfffffffful,
        execution_sequence & 0xfffffffful);
    if (session_id != 0) {
        strncpy(wash_execution->session_id, session_id, sizeof(wash_execution->session_id) - 1);
    }
    if (stage_id != 0) {
        strncpy(wash_execution->stage_id, stage_id, sizeof(wash_execution->stage_id) - 1);
    }
    wash_execution->execution_state = EXECUTION_STATE_RUNNING;
    wash_execution->execution_kind = EXECUTION_KIND_STAGE_STEP;
    wash_execution->stage_index = stage_index;
    wash_execution->started_at_ms = started_at_ms;
    wash_execution->execution_result = EXECUTION_RESULT_WAITING;
}

void wash_execution_complete(wash_execution_t *wash_execution,
    execution_result_t execution_result,
    execution_end_reason_t end_reason,
    unsigned long ended_at_ms)
{
    if (wash_execution == 0) {
        return;
    }
    wash_execution->execution_state = EXECUTION_STATE_COMPLETED;
    wash_execution->execution_result = execution_result;
    wash_execution->end_reason = end_reason;
    wash_execution->ended_at_ms = ended_at_ms;
}

void wash_execution_abort(wash_execution_t *wash_execution,
    execution_result_t execution_result,
    execution_end_reason_t end_reason,
    unsigned long ended_at_ms)
{
    if (wash_execution == 0) {
        return;
    }
    wash_execution->execution_state = EXECUTION_STATE_ABORTED;
    wash_execution->execution_result = execution_result;
    wash_execution->end_reason = end_reason;
    wash_execution->ended_at_ms = ended_at_ms;
}
