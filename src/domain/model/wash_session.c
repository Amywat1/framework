#include "domain/model/wash_session.h"

#include <stdio.h>
#include <string.h>

/**
 * @brief 重置会话到初始状态
 * @param wash_session 待重置的会话
 * @details 清零所有字段，初始化状态为 NONE、结果为 START_FAILED（未启动即视为失败）
 */
void wash_session_reset(wash_session_t *wash_session)
{
    if (wash_session == 0)
    {
        return;
    }
    memset(wash_session, 0, sizeof(*wash_session));
    wash_session->session_state = SESSION_STATE_NONE;
    wash_session->latest_execution_result = EXECUTION_RESULT_NONE;
    wash_session->final_session_result = RESULT_CODE_START_FAILED;
    wash_session->last_correlation_key[0] = '\0';
}

/**
 * @brief 创建新的洗车会话
 * @param wash_session 待初始化的会话
 * @param program_id 选中的程序 ID
 * @param program_snapshot_id 程序快照 ID
 * @param started_at_ms 启动时刻（毫秒）
 * @param session_sequence 会话序号
 * @details 生成唯一的会话 ID（时刻+序号）；状态转为 CREATED
 */
void wash_session_create(wash_session_t *wash_session, const char *program_id, const char *program_snapshot_id,
                         unsigned long started_at_ms, unsigned long session_sequence)
{
    if (wash_session == 0)
    {
        return;
    }
    wash_session_reset(wash_session);
    snprintf(wash_session->session_id, sizeof(wash_session->session_id), "session-%08lx-%08lx",
             started_at_ms & 0xfffffffful, session_sequence & 0xfffffffful);
    if (program_id != 0)
    {
        strncpy(wash_session->selected_program_id, program_id, sizeof(wash_session->selected_program_id) - 1);
    }
    if (program_snapshot_id != 0)
    {
        strncpy(wash_session->program_snapshot_id, program_snapshot_id, sizeof(wash_session->program_snapshot_id) - 1);
    }
    wash_session->session_state = SESSION_STATE_CREATED;
    wash_session->started_at_ms = started_at_ms;
}

/** @brief 将会话状态转为 RUNNING，表示首个工步已启动 */
void wash_session_start_running(wash_session_t *wash_session)
{
    if (wash_session == 0)
    {
        return;
    }
    wash_session->session_state = SESSION_STATE_RUNNING;
}

/**
 * @brief 将执行与会话关联，记录当前执行 ID 和工步进度
 * @param wash_session 会话
 * @param execution_id 当前执行 ID
 * @param segment_id 当前工步 ID
 */
void wash_session_bind_execution(wash_session_t *wash_session, const char *execution_id, const char *segment_id)
{
    if (wash_session == 0)
    {
        return;
    }
    if (execution_id != 0)
    {
        strncpy(wash_session->current_execution_id, execution_id, sizeof(wash_session->current_execution_id) - 1);
    }
    if (segment_id != 0)
    {
        strncpy(wash_session->progress_segment_id, segment_id, sizeof(wash_session->progress_segment_id) - 1);
    }
}

/** @brief 记录最新工步执行结果（用于跟踪当前工步状态） */
void wash_session_record_execution_result(wash_session_t *wash_session, execution_result_t execution_result)
{
    if (wash_session == 0)
    {
        return;
    }
    wash_session->latest_execution_result = execution_result;
}

/**
 * @brief 正常完成会话
 * @param wash_session 会话
 * @param final_session_result 最终结果码（通常为 "completed"）
 * @param ended_at_ms 结束时刻（毫秒）
 */
void wash_session_complete(wash_session_t *wash_session, result_code_t final_session_result, unsigned long ended_at_ms)
{
    if (wash_session == 0)
    {
        return;
    }
    wash_session->session_state = SESSION_STATE_COMPLETED;
    wash_session->final_session_result = final_session_result;
    wash_session->ended_at_ms = ended_at_ms;
}

/**
 * @brief 异常中止会话
 * @param wash_session 会话
 * @param final_session_result 最终结果码（通常为 "aborted"）
 * @param abort_reason 中止原因
 * @param ended_at_ms 结束时刻（毫秒）
 */
void wash_session_abort(wash_session_t *wash_session, result_code_t final_session_result, const char *abort_reason,
                        unsigned long ended_at_ms)
{
    if (wash_session == 0)
    {
        return;
    }
    wash_session->session_state = SESSION_STATE_ABORTED;
    wash_session->final_session_result = final_session_result;
    wash_session->ended_at_ms = ended_at_ms;
    if (abort_reason != 0)
    {
        strncpy(wash_session->abort_reason, abort_reason, sizeof(wash_session->abort_reason) - 1);
    }
}

bool wash_session_is_running(const wash_session_t *wash_session)
{
    return wash_session != 0 && wash_session->session_state == SESSION_STATE_RUNNING;
}

bool wash_session_is_terminal(const wash_session_t *wash_session)
{
    return wash_session != 0 && (wash_session->session_state == SESSION_STATE_COMPLETED ||
                                 wash_session->session_state == SESSION_STATE_ABORTED);
}
