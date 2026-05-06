#ifndef DOMAIN_MODEL_WASH_EXECUTION_H
#define DOMAIN_MODEL_WASH_EXECUTION_H

#include "domain/model/domain_enums.h"

/**
 * @file wash_execution.h
 * @brief 定义会话中的单次执行对象。
 */
typedef struct wash_execution_t {
    char execution_id[32];
    char session_id[32];
    execution_state_t execution_state;
    execution_kind_t execution_kind;
    int stage_index;
    char stage_id[32];
    char wait_condition_id[32];
    unsigned long started_at_ms;
    unsigned long ended_at_ms;
    execution_result_t execution_result;
    execution_end_reason_t end_reason;
} wash_execution_t;

void wash_execution_reset(wash_execution_t *wash_execution);
void wash_execution_start_stage(wash_execution_t *wash_execution,
    const char *session_id,
    int stage_index,
    const char *stage_id,
    unsigned long started_at_ms,
    unsigned long execution_sequence);
void wash_execution_complete(wash_execution_t *wash_execution,
    execution_result_t execution_result,
    execution_end_reason_t end_reason,
    unsigned long ended_at_ms);
void wash_execution_abort(wash_execution_t *wash_execution,
    execution_result_t execution_result,
    execution_end_reason_t end_reason,
    unsigned long ended_at_ms);

#endif
