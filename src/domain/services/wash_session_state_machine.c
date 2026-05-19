#include "domain/services/wash_session_state_machine.h"

#include <string.h>

#include "domain/model/runtime_state_text.h"
#include "shared/error_codes.h"

/**
 * @brief 安全写入事实字段：处理空指针、空字符串、缓冲区填充
 * @param target 目标缓冲区
 * @param target_size 缓冲区大小
 * @param value 待写入值；null 或空字符串时写 "none"
 * @details 保证 null 终止符（target_size - 1 字节有效数据）
 */
static void write_fact_field(char *target, size_t target_size, const char *value)
{
    if (target == 0 || target_size == 0)
    {
        return;
    }
    if (value != 0 && value[0] != '\0')
    {
        strncpy(target, value, target_size - 1);
        target[target_size - 1] = '\0';
        return;
    }
    strncpy(target, "none", target_size - 1);
    target[target_size - 1] = '\0';
}

/**
 * @brief 初始化会话转移事实：记录状态转移、结果码、原因码
 * @param[out] wash_session_transition_fact 输出转移事实
 * @param wash_session 转移涉及的会话
 * @param previous_state 转移前状态
 * @param current_state 转移后状态
 * @param result_code 结果码
 * @param reason_code 原因码
 */
static void init_transition_fact(wash_session_transition_fact_t *wash_session_transition_fact,
                                 const wash_session_t *wash_session, const char *previous_state,
                                 const char *current_state, const char *result_code, const char *reason_code)
{
    if (wash_session_transition_fact == 0)
    {
        return;
    }

    memset(wash_session_transition_fact, 0, sizeof(*wash_session_transition_fact));
    wash_session_transition_fact->changed = true;
    write_fact_field(wash_session_transition_fact->session_id, sizeof(wash_session_transition_fact->session_id),
                     wash_session != 0 ? wash_session->session_id : 0);
    write_fact_field(wash_session_transition_fact->previous_state, sizeof(wash_session_transition_fact->previous_state),
                     previous_state);
    write_fact_field(wash_session_transition_fact->current_state, sizeof(wash_session_transition_fact->current_state),
                     current_state);
    write_fact_field(wash_session_transition_fact->result_code, sizeof(wash_session_transition_fact->result_code),
                     result_code);
    write_fact_field(wash_session_transition_fact->reason_code, sizeof(wash_session_transition_fact->reason_code),
                     reason_code);
}

/**
 * @brief 验证会话服务参数的合法性
 * @param wash_session_service_args 会话服务参数
 * @param wash_session_transition_fact 输出事实缓冲区
 * @return true 表示任意参数为 null；false 表示全部合法
 */
static bool invalid_args(wash_session_service_args_t *wash_session_service_args,
                         wash_session_transition_fact_t *wash_session_transition_fact)
{
    return wash_session_service_args == 0 || wash_session_service_args->wash_session == 0 ||
           wash_session_transition_fact == 0;
}

/**
 * @brief 创建并启动新的运行会话。
 * @param wash_session_service_args 会话服务参数。
 * @param program_id 目标程序 ID。
 * @param wash_session_transition_fact 输出迁移事实。
 * @return 成功返回 `operation_result_ok()`，失败时返回对应错误。
 */
operation_result_t wash_session_state_machine_start(wash_session_service_args_t *wash_session_service_args,
                                                    const char *program_id,
                                                    wash_session_transition_fact_t *wash_session_transition_fact)
{
    const char *previous_state;

    if (invalid_args(wash_session_service_args, wash_session_transition_fact) || program_id == 0 ||
        wash_session_service_args->program_snapshot == 0 || wash_session_service_args->next_session_sequence == 0)
    {
        return operation_result_fail(ERROR_CODE_INVALID_ARGUMENT);
    }
    if (wash_session_is_running(wash_session_service_args->wash_session))
    {
        return operation_result_fail(ERROR_CODE_INVALID_STATE);
    }

    previous_state = runtime_state_text_session_state(wash_session_service_args->wash_session->session_state);
    *wash_session_service_args->next_session_sequence += 1;
    wash_session_create(wash_session_service_args->wash_session, program_id,
                        wash_session_service_args->program_snapshot->program_snapshot_id,
                        wash_session_service_args->current_time_ms, *wash_session_service_args->next_session_sequence);
    wash_session_start_running(wash_session_service_args->wash_session);
    init_transition_fact(wash_session_transition_fact, wash_session_service_args->wash_session, previous_state,
                         runtime_state_text_session_state(wash_session_service_args->wash_session->session_state), "accepted",
                         "session_started");
    return operation_result_ok();
}

/**
 * @brief 将当前会话推进到完成态。
 * @param wash_session_service_args 会话服务参数。
 * @param result_code 完成结果码。
 * @param wash_session_transition_fact 输出迁移事实。
 * @return 成功返回 `operation_result_ok()`，失败时返回对应错误。
 */
operation_result_t wash_session_state_machine_complete(wash_session_service_args_t *wash_session_service_args,
                                                       result_code_t result_code,
                                                       wash_session_transition_fact_t *wash_session_transition_fact)
{
    const char *previous_state;

    if (invalid_args(wash_session_service_args, wash_session_transition_fact))
    {
        return operation_result_fail(ERROR_CODE_INVALID_ARGUMENT);
    }

    previous_state = runtime_state_text_session_state(wash_session_service_args->wash_session->session_state);
    wash_session_complete(wash_session_service_args->wash_session, result_code,
                          wash_session_service_args->current_time_ms);
    init_transition_fact(wash_session_transition_fact, wash_session_service_args->wash_session, previous_state,
                         runtime_state_text_session_state(wash_session_service_args->wash_session->session_state), "completed",
                         "program_finished");
    return operation_result_ok();
}

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
                                                    wash_session_transition_fact_t *wash_session_transition_fact)
{
    const char *previous_state;

    if (invalid_args(wash_session_service_args, wash_session_transition_fact))
    {
        return operation_result_fail(ERROR_CODE_INVALID_ARGUMENT);
    }

    previous_state = runtime_state_text_session_state(wash_session_service_args->wash_session->session_state);
    wash_session_abort(wash_session_service_args->wash_session, result_code, reason_code,
                       wash_session_service_args->current_time_ms);
    init_transition_fact(wash_session_transition_fact, wash_session_service_args->wash_session, previous_state,
                         runtime_state_text_session_state(wash_session_service_args->wash_session->session_state), "aborted",
                         reason_code);
    return operation_result_ok();
}
