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
    bool global_fault_present;
    char session_id[32];
    session_state_t session_state;
    execution_state_t execution_state;
    char stage_id[32];
    char wait_condition_id[32];
    char wait_reason[64];
    char reason_code[64];
    char global_fault_reason[128];
} wash_session_status_view_t;

/**
 * @brief 读取主控当前的状态快照。
 *
 * @param system_context 主控共享上下文，不能为空。
 * @param wash_session_status_view 输出状态视图，不能为空。
 * @return 成功返回 `operation_result_ok()`；参数非法时返回失败结果。
 *
 * @note 本接口是只读操作，不得推进业务状态。
 */
operation_result_t query_wash_session_status_execute(system_context_t *system_context, wash_session_status_view_t *wash_session_status_view);

#endif
