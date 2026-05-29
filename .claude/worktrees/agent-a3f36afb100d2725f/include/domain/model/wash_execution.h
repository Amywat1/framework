#ifndef DOMAIN_MODEL_WASH_EXECUTION_H
#define DOMAIN_MODEL_WASH_EXECUTION_H

#include <stdbool.h>

#include "domain/model/conditional_control.h"
#include "domain/model/domain_enums.h"
#include "domain/model/exit_action.h"

/**
 * @file wash_execution.h
 * @brief 定义当前工步段运行态。
 */
/**
 * @brief 描述退出动作在运行期的执行进度。
 */
typedef struct exit_action_runtime_t
{
    /**< 顶刷停止动作是否已触发。 */
    bool stop_roof_brush_started;
    /**< 侧刷停止动作是否已触发。 */
    bool stop_side_brush_started;
    /**< 化学剂停止动作是否已触发。 */
    bool stop_chemical_started;
    /**< 纯水关闭动作是否已触发。 */
    bool close_ro_water_started;
    /**< 风机关闭动作是否已触发。 */
    bool close_dryer_started;
    /**< 顶刷回零动作是否已触发。 */
    bool roof_brush_home_started;
    /**< 顶刷停止是否已完成。 */
    bool stop_roof_brush_done;
    /**< 侧刷停止是否已完成。 */
    bool stop_side_brush_done;
    /**< 化学剂停止是否已完成。 */
    bool stop_chemical_done;
    /**< 纯水关闭是否已完成。 */
    bool close_ro_water_done;
    /**< 风机关闭是否已完成。 */
    bool close_dryer_done;
    /**< 顶刷回零是否已完成。 */
    bool roof_brush_home_done;
} exit_action_runtime_t;

/**
 * @brief 描述当前工步段执行的完整运行态。
 */
typedef struct wash_execution_t
{
    /**< 执行实例 ID。 */
    char execution_id[32];
    /**< 关联会话 ID。 */
    char session_id[32];
    /**< 执行总状态。 */
    execution_state_t execution_state;
    /**< 当前段生命周期状态。 */
    segment_lifecycle_state_t lifecycle_state;
    /**< 当前工步段索引。 */
    int segment_index;
    /**< 当前工步段 ID。 */
    char segment_id[32];
    /**< 各条件控制当前是否处于激活态。 */
    bool active_conditional_controls[MAX_SEGMENT_CONDITIONAL_CONTROLS];
    /**< 退出动作运行期状态。 */
    exit_action_runtime_t exit_runtime;
    /**< 执行开始时间。 */
    unsigned long started_at_ms;
    /**< 当前工步段开始时间。 */
    unsigned long segment_started_at_ms;
    /**< 退出阶段开始时间。 */
    unsigned long exit_started_at_ms;
    /**< 执行结束时间。 */
    unsigned long ended_at_ms;
    /**< 执行结果。 */
    execution_result_t execution_result;
    /**< 执行结束原因。 */
    execution_end_reason_t end_reason;
    /**< 结果码。 */
    char result_code[32];
    /**< 原因码。 */
    char reason_code[64];
} wash_execution_t;

/**
 * @brief 重置当前执行对象。
 *
 * @param wash_execution 执行对象，不能为空。
 *
 * @note 本对象只解释当前工步执行态，不解释整个会话的最终结论。
 */
void wash_execution_reset(wash_execution_t *wash_execution);
/**
 * @brief 以指定工步段信息启动新的执行。
 * @param wash_execution 执行对象，不能为空。
 * @param session_id 关联会话 ID，可为空。
 * @param segment_index 工步段索引。
 * @param segment_id 工步段 ID，可为空。
 * @param started_at_ms 开始时间。
 * @param execution_sequence 执行序列号。
 */
void wash_execution_start_segment(wash_execution_t *wash_execution, const char *session_id, int segment_index,
                                  const char *segment_id, unsigned long started_at_ms,
                                  unsigned long execution_sequence);
/**
 * @brief 将执行推进到运行态。
 * @param wash_execution 执行对象，不能为空。
 */
void wash_execution_mark_running(wash_execution_t *wash_execution);
/**
 * @brief 将执行推进到退出阶段。
 * @param wash_execution 执行对象，不能为空。
 * @param current_time_ms 当前时间。
 */
void wash_execution_begin_exit(wash_execution_t *wash_execution, unsigned long current_time_ms);
/**
 * @brief 以完成结论结束当前执行。
 * @param wash_execution 执行对象，不能为空。
 * @param execution_result 执行结果。
 * @param end_reason 执行结束原因。
 * @param ended_at_ms 结束时间。
 */
void wash_execution_complete(wash_execution_t *wash_execution, execution_result_t execution_result,
                             execution_end_reason_t end_reason, unsigned long ended_at_ms);
/**
 * @brief 以中止结论结束当前执行。
 * @param wash_execution 执行对象，不能为空。
 * @param execution_result 执行结果。
 * @param end_reason 执行结束原因。
 * @param ended_at_ms 结束时间。
 */
void wash_execution_abort(wash_execution_t *wash_execution, execution_result_t execution_result,
                          execution_end_reason_t end_reason, unsigned long ended_at_ms);

#endif
