#ifndef DOMAIN_MODEL_WASH_SESSION_H
#define DOMAIN_MODEL_WASH_SESSION_H

#include <stdbool.h>

#include "domain/model/domain_enums.h"

/**
 * @file wash_session.h
 * @brief 定义洗车会话对象。
 */
typedef struct wash_session_t {
    char session_id[32];
    session_state_t session_state;
    char selected_program_id[32];
    char program_snapshot_id[32];
    char current_execution_id[32];
    char progress_segment_id[32];
    execution_result_t latest_execution_result;
    result_code_t final_session_result;
    char abort_reason[64];
    char last_correlation_key[64];
    unsigned long started_at_ms;
    unsigned long ended_at_ms;
} wash_session_t;

void wash_session_reset(wash_session_t *wash_session);
void wash_session_create(wash_session_t *wash_session,
    const char *program_id,
    const char *program_snapshot_id,
    unsigned long started_at_ms,
    unsigned long session_sequence);
void wash_session_start_running(wash_session_t *wash_session);
void wash_session_bind_execution(wash_session_t *wash_session, const char *execution_id, const char *segment_id);
void wash_session_record_execution_result(wash_session_t *wash_session, execution_result_t execution_result);
void wash_session_complete(wash_session_t *wash_session, result_code_t final_session_result, unsigned long ended_at_ms);
void wash_session_abort(wash_session_t *wash_session, result_code_t final_session_result, const char *abort_reason, unsigned long ended_at_ms);
bool wash_session_is_running(const wash_session_t *wash_session);
bool wash_session_is_terminal(const wash_session_t *wash_session);

#endif
