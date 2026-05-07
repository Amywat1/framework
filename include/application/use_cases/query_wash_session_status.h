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

/**
 * @brief 描述主控当前的只读状态快照。
 */
typedef struct wash_session_status_view_t {
    /** @brief 当前是否存在活动中的会话。 */
    bool has_active_session;
    /** @brief 当前是否存在全局故障。 */
    bool global_fault_present;
    /** @brief 当前会话标识；无活动会话时为空字符串。 */
    char session_id[32];
    /** @brief 当前会话状态。 */
    session_state_t session_state;
    /** @brief 当前执行状态。 */
    execution_state_t execution_state;
    /** @brief 当前推进到的阶段标识。 */
    char stage_id[32];
    /** @brief 当前等待条件标识。 */
    char wait_condition_id[32];
    /** @brief 当前等待原因或期待信号。 */
    char wait_reason[64];
    /** @brief 最近一次处理事件的原因码。 */
    char reason_code[64];
    /** @brief 最近一次全局故障原因。 */
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
