#ifndef DOMAIN_SERVICES_WASH_SESSION_STATE_MACHINE_H
#define DOMAIN_SERVICES_WASH_SESSION_STATE_MACHINE_H

#include <stdbool.h>

#include "domain/model/program_snapshot.h"
#include "domain/model/wash_session.h"
#include "shared/result_types.h"

/**
 * @file wash_session_state_machine.h
 * @brief 定义会话状态迁移规则。
 */
typedef struct wash_session_transition_fact_t
{
    bool changed;
    char session_id[32];
    char previous_state[16];
    char current_state[16];
    char result_code[32];
    char reason_code[64];
} wash_session_transition_fact_t;

typedef struct wash_session_service_args_t
{
    wash_session_t *wash_session;
    const program_snapshot_t *program_snapshot;
    unsigned long *next_session_sequence;
    unsigned long current_time_ms;
} wash_session_service_args_t;

/**
 * @brief 会话状态机的最小依赖切片。
 *
 * @note 本切片不得携带 `system_context_t`、全局故障、最近结果或触发队列等无关共享状态。
 */

operation_result_t wash_session_state_machine_start(wash_session_service_args_t *wash_session_service_args,
                                                    const char *program_id,
                                                    wash_session_transition_fact_t *wash_session_transition_fact);
operation_result_t wash_session_state_machine_complete(wash_session_service_args_t *wash_session_service_args,
                                                       result_code_t result_code,
                                                       wash_session_transition_fact_t *wash_session_transition_fact);
operation_result_t wash_session_state_machine_abort(wash_session_service_args_t *wash_session_service_args,
                                                    result_code_t result_code, const char *reason_code,
                                                    wash_session_transition_fact_t *wash_session_transition_fact);

#endif
