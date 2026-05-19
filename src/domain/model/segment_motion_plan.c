#include "domain/model/segment_motion_plan.h"

#include <string.h>

/**
 * @brief 初始化移动计划对象。
 * @param segment_motion_plan 移动计划对象。
 */
void segment_motion_plan_init(segment_motion_plan_t *segment_motion_plan)
{
    if (segment_motion_plan == 0)
    {
        return;
    }

    memset(segment_motion_plan, 0, sizeof(*segment_motion_plan));
    segment_motion_plan->target_tolerance_mm = 50;
}

/**
 * @brief 判断移动计划配置是否合法。
 * @param segment_motion_plan 移动计划对象。
 * @return 合法返回 `true`，否则返回 `false`。
 */
bool segment_motion_plan_is_valid(const segment_motion_plan_t *segment_motion_plan)
{
    if (segment_motion_plan == 0)
    {
        return false;
    }
    if (segment_motion_plan->target_reference == MOTION_TARGET_NONE)
    {
        return false;
    }
    if (segment_motion_plan->target_reference == MOTION_TARGET_RELATIVE_DISTANCE &&
        segment_motion_plan->relative_basis == POSITION_REFERENCE_NONE)
    {
        return false;
    }
    if (segment_motion_plan->direction == MOTION_DIRECTION_STOP &&
        segment_motion_plan->target_reference != MOTION_TARGET_HOME)
    {
        return false;
    }
    return true;
}
