#ifndef DOMAIN_MODEL_WASH_SEGMENT_H
#define DOMAIN_MODEL_WASH_SEGMENT_H

#include <stdbool.h>

#include "domain/model/conditional_control.h"
#include "domain/model/domain_enums.h"
#include "domain/model/exit_action.h"
#include "domain/model/position_trigger.h"
#include "domain/model/segment_exception_policy.h"
#include "domain/model/segment_motion_plan.h"

/**
 * @file wash_segment.h
 * @brief 定义强类型工步段模型。
 */
typedef struct segment_continuous_controls_t
{
    bool roof_brush_follow;
    bool side_brush_enabled;
    bool ro_water_enabled;
    bool dryer_enabled;
} segment_continuous_controls_t;

typedef struct segment_completion_condition_t
{
    position_trigger_t trigger;
    bool requires_all_controls_started;
} segment_completion_condition_t;

typedef struct wash_segment_t
{
    char segment_id[32];
    char segment_name[32];
    int sequence_no;
    segment_kind_t segment_kind;
    segment_motion_plan_t motion_plan;
    segment_continuous_controls_t continuous_controls;
    conditional_control_t conditional_controls[MAX_SEGMENT_CONDITIONAL_CONTROLS];
    int conditional_control_count;
    segment_completion_condition_t completion_condition;
    segment_exit_actions_t exit_actions;
    int segment_timeout_ms;
    int exit_timeout_ms;
    segment_exception_policy_t exception_policy;
} wash_segment_t;

void wash_segment_init(wash_segment_t *wash_segment, const char *segment_id, const char *segment_name, int sequence_no);
bool wash_segment_is_valid(const wash_segment_t *wash_segment);

#endif
