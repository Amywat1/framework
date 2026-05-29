#include "domain/model/wash_execution.h"

#include <stdio.h>
#include <string.h>

/**
 * @brief 重置工步执行态到初始状态
 * @param wash_execution 待重置的执行态
 * @details 清零所有字段，初始化生命周期为 PENDING、执行状态为 NONE、工步索引为 -1
 */
void wash_execution_reset(wash_execution_t *wash_execution)
{
    if (wash_execution == 0)
    {
        return;
    }
    memset(wash_execution, 0, sizeof(*wash_execution));
    wash_execution->execution_state = EXECUTION_STATE_NONE;
    wash_execution->lifecycle_state = SEGMENT_LIFECYCLE_PENDING;
    wash_execution->execution_result = EXECUTION_RESULT_NONE;
    wash_execution->end_reason = EXECUTION_END_REASON_NONE;
    wash_execution->segment_index = -1;
}

/**
 * @brief 启动新工步的执行
 * @param wash_execution 执行态
 * @param session_id 关联的会话 ID
 * @param segment_index 工步索引
 * @param segment_id 工步 ID
 * @param started_at_ms 启动时刻（毫秒）
 * @param execution_sequence 执行序号
 * @details 生成唯一执行 ID；状态转为 RUNNING + ENTERING；初始结果为 "entering/segment_loaded"
 */
void wash_execution_start_segment(wash_execution_t *wash_execution, const char *session_id, int segment_index,
                                  const char *segment_id, unsigned long started_at_ms, unsigned long execution_sequence)
{
    if (wash_execution == 0)
    {
        return;
    }
    wash_execution_reset(wash_execution);
    snprintf(wash_execution->execution_id, sizeof(wash_execution->execution_id), "exec-%08lx-%08lx",
             started_at_ms & 0xfffffffful, execution_sequence & 0xfffffffful);
    if (session_id != 0)
    {
        strncpy(wash_execution->session_id, session_id, sizeof(wash_execution->session_id) - 1);
    }
    if (segment_id != 0)
    {
        strncpy(wash_execution->segment_id, segment_id, sizeof(wash_execution->segment_id) - 1);
    }
    wash_execution->execution_state = EXECUTION_STATE_RUNNING;
    wash_execution->lifecycle_state = SEGMENT_LIFECYCLE_ENTERING;
    wash_execution->segment_index = segment_index;
    wash_execution->started_at_ms = started_at_ms;
    wash_execution->segment_started_at_ms = started_at_ms;
    strncpy(wash_execution->result_code, "entering", sizeof(wash_execution->result_code) - 1);
    strncpy(wash_execution->reason_code, "segment_loaded", sizeof(wash_execution->reason_code) - 1);
}

/** @brief 将工步生命周期转为 RUNNING，表示已进入执行主体 */
void wash_execution_mark_running(wash_execution_t *wash_execution)
{
    if (wash_execution == 0)
    {
        return;
    }
    wash_execution->lifecycle_state = SEGMENT_LIFECYCLE_RUNNING;
    wash_execution->execution_result = EXECUTION_RESULT_RUNNING;
    strncpy(wash_execution->result_code, "running", sizeof(wash_execution->result_code) - 1);
    strncpy(wash_execution->reason_code, "segment_running", sizeof(wash_execution->reason_code) - 1);
}

/**
 * @brief 进入工步出口阶段（停止执行、运行出口行为）
 * @param wash_execution 执行态
 * @param current_time_ms 当前时刻（毫秒）
 * @details 清空出口运行时快照，记录出口启动时刻
 */
void wash_execution_begin_exit(wash_execution_t *wash_execution, unsigned long current_time_ms)
{
    if (wash_execution == 0)
    {
        return;
    }
    wash_execution->lifecycle_state = SEGMENT_LIFECYCLE_EXITING;
    wash_execution->exit_started_at_ms = current_time_ms;
    memset(&wash_execution->exit_runtime, 0, sizeof(wash_execution->exit_runtime));
    strncpy(wash_execution->result_code, "exiting", sizeof(wash_execution->result_code) - 1);
    strncpy(wash_execution->reason_code, "segment_complete", sizeof(wash_execution->reason_code) - 1);
}

/**
 * @brief 正常完成工步执行
 * @param wash_execution 执行态
 * @param execution_result 执行结果（运行成功、超时等）
 * @param end_reason 结束原因（正常完成、时间限制等）
 * @param ended_at_ms 结束时刻（毫秒）
 */
void wash_execution_complete(wash_execution_t *wash_execution, execution_result_t execution_result,
                             execution_end_reason_t end_reason, unsigned long ended_at_ms)
{
    if (wash_execution == 0)
    {
        return;
    }
    wash_execution->execution_state = EXECUTION_STATE_COMPLETED;
    wash_execution->lifecycle_state = SEGMENT_LIFECYCLE_COMPLETED;
    wash_execution->execution_result = execution_result;
    wash_execution->end_reason = end_reason;
    wash_execution->ended_at_ms = ended_at_ms;
}

/**
 * @brief 异常中止工步执行
 * @param wash_execution 执行态
 * @param execution_result 执行结果（故障、恢复失败等）
 * @param end_reason 中止原因（传感器故障、恢复失败等）
 * @param ended_at_ms 结束时刻（毫秒）
 */
void wash_execution_abort(wash_execution_t *wash_execution, execution_result_t execution_result,
                          execution_end_reason_t end_reason, unsigned long ended_at_ms)
{
    if (wash_execution == 0)
    {
        return;
    }
    wash_execution->execution_state = EXECUTION_STATE_ABORTED;
    wash_execution->lifecycle_state = SEGMENT_LIFECYCLE_ABORTED;
    wash_execution->execution_result = execution_result;
    wash_execution->end_reason = end_reason;
    wash_execution->ended_at_ms = ended_at_ms;
}
