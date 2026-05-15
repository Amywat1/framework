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
typedef struct exit_action_runtime_t
{
    bool stop_roof_brush_started;
    bool stop_side_brush_started;
    bool stop_chemical_started;
    bool close_ro_water_started;
    bool close_dryer_started;
    bool roof_brush_home_started;
    bool stop_roof_brush_done;
    bool stop_side_brush_done;
    bool stop_chemical_done;
    bool close_ro_water_done;
    bool close_dryer_done;
    bool roof_brush_home_done;
} exit_action_runtime_t;

typedef struct wash_execution_t
{
    char execution_id[32];
    char session_id[32];
    execution_state_t execution_state;
    segment_lifecycle_state_t lifecycle_state;
    int segment_index;
    char segment_id[32];
    bool active_conditional_controls[MAX_SEGMENT_CONDITIONAL_CONTROLS];
    exit_action_runtime_t exit_runtime;
    unsigned long started_at_ms;
    unsigned long segment_started_at_ms;
    unsigned long exit_started_at_ms;
    unsigned long ended_at_ms;
    execution_result_t execution_result;
    execution_end_reason_t end_reason;
    char result_code[32];
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
void wash_execution_start_segment(wash_execution_t *wash_execution, const char *session_id, int segment_index,
                                  const char *segment_id, unsigned long started_at_ms,
                                  unsigned long execution_sequence);
void wash_execution_mark_running(wash_execution_t *wash_execution);
void wash_execution_begin_exit(wash_execution_t *wash_execution, unsigned long current_time_ms);
void wash_execution_complete(wash_execution_t *wash_execution, execution_result_t execution_result,
                             execution_end_reason_t end_reason, unsigned long ended_at_ms);
void wash_execution_abort(wash_execution_t *wash_execution, execution_result_t execution_result,
                          execution_end_reason_t end_reason, unsigned long ended_at_ms);

#endif
