#ifndef DOMAIN_MODEL_SEGMENT_MOTION_PLAN_H
#define DOMAIN_MODEL_SEGMENT_MOTION_PLAN_H

#include <stdbool.h>

#include "domain/model/domain_enums.h"

/**
 * @file segment_motion_plan.h
 * @brief 定义工步段移动计划。
 */
typedef struct segment_motion_plan_t {
    motion_direction_t direction;
    motion_target_reference_t target_reference;
    position_reference_t relative_basis;
    int target_distance_mm;
    int target_tolerance_mm;
    bool requires_position_feedback;
} segment_motion_plan_t;

void segment_motion_plan_init(segment_motion_plan_t *segment_motion_plan);
bool segment_motion_plan_is_valid(const segment_motion_plan_t *segment_motion_plan);

#endif
