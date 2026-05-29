#ifndef DOMAIN_MODEL_SEGMENT_MOTION_PLAN_H
#define DOMAIN_MODEL_SEGMENT_MOTION_PLAN_H

#include <stdbool.h>

#include "domain/model/domain_enums.h"

/**
 * @file segment_motion_plan.h
 * @brief 定义工步段移动计划。
 */
/**
 * @brief 描述一段工步的移动目标与约束。
 */
typedef struct segment_motion_plan_t
{
    /**< 移动方向。 */
    motion_direction_t direction;
    /**< 目标参考量。 */
    motion_target_reference_t target_reference;
    /**< 相对目标使用的基准参考量。 */
    position_reference_t relative_basis;
    /**< 目标距离，单位毫米。 */
    int target_distance_mm;
    /**< 目标容差，单位毫米。 */
    int target_tolerance_mm;
    /**< 是否要求位置反馈参与闭环。 */
    bool requires_position_feedback;
} segment_motion_plan_t;

/**
 * @brief 将移动计划初始化为零值状态。
 * @param segment_motion_plan 移动计划对象，不能为空。
 */
void segment_motion_plan_init(segment_motion_plan_t *segment_motion_plan);
/**
 * @brief 判断移动计划配置是否合法。
 * @param segment_motion_plan 移动计划对象。
 * @return 合法返回 `true`，否则返回 `false`。
 */
bool segment_motion_plan_is_valid(const segment_motion_plan_t *segment_motion_plan);

#endif
