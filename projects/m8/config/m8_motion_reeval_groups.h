/**
 * @file    m8_motion_reeval_groups.h
 * @brief   M8 机型 ON_MOTION 重评估分组 ID
 * @author  HUWANGWEI
 * @date    2026-07-10
 */

#ifndef CONFIG_M8_MOTION_REEVAL_GROUPS_H
#define CONFIG_M8_MOTION_REEVAL_GROUPS_H

#include "framework/domain/safety/model/alarm_types.h"

typedef enum
{
    M8_REEVAL_GROUP_NONE = ALARM_REEVAL_GROUP_NONE,
    M8_REEVAL_GROUP_GANTRY,
    M8_REEVAL_GROUP_TOP_BRUSH,
    M8_REEVAL_GROUP_SIDE_BRUSH,
    M8_REEVAL_GROUP_FRONT_WHEEL,
    M8_REEVAL_GROUP_REAR_WHEEL_LOCK,
    M8_REEVAL_GROUP_FAN,
    M8_REEVAL_GROUP_WATER,
    M8_REEVAL_GROUP_COUNT
} m8_motion_reeval_group_t;

#endif /* CONFIG_M8_MOTION_REEVAL_GROUPS_H */
