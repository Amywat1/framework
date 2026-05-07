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
 * @brief 描述会话状态机完成一次迁移后返回给应用层的事实。
 */
typedef struct wash_session_transition_fact_t {
    bool changed;
    char session_id[32];
    char previous_state[16];
    char current_state[16];
    char result_code[32];
    char reason_code[64];
} wash_session_transition_fact_t;

/**
 * @brief 描述会话状态机推进所需的最小输入集合。
 */
typedef struct wash_session_service_args_t {
    wash_session_t *wash_session;
    const program_snapshot_t *program_snapshot;
    unsigned long *next_session_sequence;
    unsigned long current_time_ms;
} wash_session_service_args_t;

/**
 * @brief 启动一个新的运行中会话。
 *
 * @param wash_session_service_args 会话状态机输入，不能为空。
 * @param program_id 目标程序标识，不能为空。
 * @param wash_session_transition_fact 输出会话迁移事实，不能为空。
 * @return 成功返回 `operation_result_ok()`；参数非法或状态非法时返回失败结果。
 */
operation_result_t wash_session_state_machine_start(wash_session_service_args_t *wash_session_service_args,
    const char *program_id,
    wash_session_transition_fact_t *wash_session_transition_fact);

/**
 * @brief 将当前会话推进到完成态。
 *
 * @param wash_session_service_args 会话状态机输入，不能为空。
 * @param result_code 会话最终结果码。
 * @param wash_session_transition_fact 输出会话迁移事实，不能为空。
 * @return 成功返回 `operation_result_ok()`；参数非法时返回失败结果。
 */
operation_result_t wash_session_state_machine_complete(wash_session_service_args_t *wash_session_service_args,
    result_code_t result_code,
    wash_session_transition_fact_t *wash_session_transition_fact);

/**
 * @brief 将当前会话推进到中止态。
 *
 * @param wash_session_service_args 会话状态机输入，不能为空。
 * @param result_code 会话最终结果码。
 * @param reason_code 中止原因码；为空时写入 `"none"`。
 * @param wash_session_transition_fact 输出会话迁移事实，不能为空。
 * @return 成功返回 `operation_result_ok()`；参数非法时返回失败结果。
 */
operation_result_t wash_session_state_machine_abort(wash_session_service_args_t *wash_session_service_args,
    result_code_t result_code,
    const char *reason_code,
    wash_session_transition_fact_t *wash_session_transition_fact);

#endif
