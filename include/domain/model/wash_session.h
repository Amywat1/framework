#ifndef DOMAIN_MODEL_WASH_SESSION_H
#define DOMAIN_MODEL_WASH_SESSION_H

#include <stdbool.h>

#include "domain/model/domain_enums.h"

/**
 * @file wash_session.h
 * @brief 定义洗车会话对象。
 */
/**
 * @brief 描述一次洗车会话的运行状态。
 */
typedef struct wash_session_t
{
    /**< 会话 ID。 */
    char session_id[32];
    /**< 当前会话状态。 */
    session_state_t session_state;
    /**< 选择的程序 ID。 */
    char selected_program_id[32];
    /**< 绑定的程序快照 ID。 */
    char program_snapshot_id[32];
    /**< 当前执行 ID。 */
    char current_execution_id[32];
    /**< 当前进度工步段 ID。 */
    char progress_segment_id[32];
    /**< 最近一次执行结果。 */
    execution_result_t latest_execution_result;
    /**< 会话最终结果。 */
    result_code_t final_session_result;
    /**< 中止原因。 */
    char abort_reason[64];
    /**< 最近关联键。 */
    char last_correlation_key[64];
    /**< 开始时间。 */
    unsigned long started_at_ms;
    /**< 结束时间。 */
    unsigned long ended_at_ms;
} wash_session_t;

/**
 * @brief 重置会话对象到未运行状态。
 *
 * @param wash_session 会话对象，不能为空。
 *
 * @note `final_session_result` 是会话最终结论的唯一正式落点。
 */
void wash_session_reset(wash_session_t *wash_session);
/**
 * @brief 创建新的洗车会话。
 * @param wash_session 会话对象，不能为空。
 * @param program_id 选择的程序 ID，可为空。
 * @param program_snapshot_id 绑定的程序快照 ID，可为空。
 * @param started_at_ms 开始时间。
 * @param session_sequence 会话序列号。
 */
void wash_session_create(wash_session_t *wash_session, const char *program_id, const char *program_snapshot_id,
                         unsigned long started_at_ms, unsigned long session_sequence);
/**
 * @brief 将会话推进到运行态。
 * @param wash_session 会话对象，不能为空。
 */
void wash_session_start_running(wash_session_t *wash_session);
/**
 * @brief 将当前执行绑定到会话进度上。
 * @param wash_session 会话对象，不能为空。
 * @param execution_id 执行 ID，可为空。
 * @param segment_id 工步段 ID，可为空。
 */
void wash_session_bind_execution(wash_session_t *wash_session, const char *execution_id, const char *segment_id);
/**
 * @brief 记录最近一次执行结果。
 * @param wash_session 会话对象，不能为空。
 * @param execution_result 最近执行结果。
 */
void wash_session_record_execution_result(wash_session_t *wash_session, execution_result_t execution_result);
/**
 * @brief 以最终完成结论结束会话。
 *
 * @param wash_session 会话对象，不能为空。
 * @param final_session_result 会话最终结果。
 * @param ended_at_ms 结束时间。
 */
void wash_session_complete(wash_session_t *wash_session, result_code_t final_session_result, unsigned long ended_at_ms);

/**
 * @brief 以中止结论结束会话。
 *
 * @param wash_session 会话对象，不能为空。
 * @param final_session_result 会话最终结果。
 * @param abort_reason 中止原因，可为空。
 * @param ended_at_ms 结束时间。
 */
void wash_session_abort(wash_session_t *wash_session, result_code_t final_session_result, const char *abort_reason,
                        unsigned long ended_at_ms);
/**
 * @brief 判断会话当前是否处于运行中。
 * @param wash_session 会话对象。
 * @return 运行中返回 `true`，否则返回 `false`。
 */
bool wash_session_is_running(const wash_session_t *wash_session);
/**
 * @brief 判断会话是否已进入终态。
 * @param wash_session 会话对象。
 * @return 已完成或已中止返回 `true`，否则返回 `false`。
 */
bool wash_session_is_terminal(const wash_session_t *wash_session);

#endif
