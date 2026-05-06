#ifndef APPLICATION_USE_CASES_QUERY_WASH_SESSION_STATUS_H
#define APPLICATION_USE_CASES_QUERY_WASH_SESSION_STATUS_H

#include <stdbool.h>

#include "application/coordinators/system_context.h"
#include "domain/model/domain_enums.h"
#include "shared/result_types.h"

/**
 * @file query_wash_session_status.h
 * @brief 定义统一状态查询入口。
 */
typedef struct wash_session_status_view_t {
    bool has_active_session;
    char session_id[32];
    session_state_t session_state;
    execution_state_t execution_state;
    char stage_id[32];
    char reason_code[64];
} wash_session_status_view_t;

operation_result_t query_wash_session_status_execute(system_context_t *system_context, wash_session_status_view_t *wash_session_status_view);

#endif
