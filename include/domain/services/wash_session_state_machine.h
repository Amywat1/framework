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
/**
 * @brief 描述一次会话状态迁移产生的事实。
 */
typedef struct wash_session_transition_fact_t
{
    /**< 本次迁移是否产生状态变化。 */
    bool changed;
    /**< 当前会话 ID。 */
    char session_id[32];
    /**< 迁移前的会话状态。 */
    char previous_state[16];
    /**< 迁移后的会话状态。 */
    char current_state[16];
    /**< 本次迁移对应的结果码。 */
    char result_code[32];
    /**< 本次迁移对应的原因码。 */
    char reason_code[64];
} wash_session_transition_fact_t;

/**
 * @brief 描述会话状态机所需的最小依赖切片。
 */
typedef struct wash_session_service_args_t
{
    /**< 当前会话对象。 */
    wash_session_t *wash_session;
    /**< 当前绑定的程序快照。 */
    const program_snapshot_t *program_snapshot;
    /**< 会话序列号分配器。 */
    unsigned long *next_session_sequence;
    /**< 当前运行时间。 */
    unsigned long current_time_ms;
} wash_session_service_args_t;

/**
 * @brief 会话状态机的最小依赖切片。
 *
 * @note 本切片不得携带 `control_context_t`、全局故障、最近结果或触发队列等无关共享状态。
 */
/**
 * @brief 创建并启动新的运行会话。
 * @param wash_session_service_args 会话服务参数。
 * @param program_id 目标程序 ID。
 * @param wash_session_transition_fact 输出迁移事实。
 * @return 成功返回 `operation_result_ok()`，参数非法或状态不满足时返回失败结果。
 */
operation_result_t wash_session_state_machine_start(wash_session_service_args_t *wash_session_service_args,
                                                    const char *program_id,
                                                    wash_session_transition_fact_t *wash_session_transition_fact);
/**
 * @brief 将当前会话推进到完成态。
 * @param wash_session_service_args 会话服务参数。
 * @param result_code 完成结果码。
 * @param wash_session_transition_fact 输出迁移事实。
 * @return 成功返回 `operation_result_ok()`，失败时返回对应错误。
 */
operation_result_t wash_session_state_machine_complete(wash_session_service_args_t *wash_session_service_args,
                                                       result_code_t result_code,
                                                       wash_session_transition_fact_t *wash_session_transition_fact);
/**
 * @brief 将当前会话推进到中止态。
 * @param wash_session_service_args 会话服务参数。
 * @param result_code 中止结果码。
 * @param reason_code 中止原因码。
 * @param wash_session_transition_fact 输出迁移事实。
 * @return 成功返回 `operation_result_ok()`，失败时返回对应错误。
 */
operation_result_t wash_session_state_machine_abort(wash_session_service_args_t *wash_session_service_args,
                                                    result_code_t result_code, const char *reason_code,
                                                    wash_session_transition_fact_t *wash_session_transition_fact);

#endif
