/**
 * @file    m8_alarm_reeval_bindings.c
 * @brief   M8 ON_MOTION 触发源 → 重评估分组映射表
 * @author  HUWANGWEI
 * @date    2026-07-10
 */

#include "projects/m8/wiring/m8_alarm_reeval_bindings.h"
#include "projects/m8/config/m8_motion_reeval_groups.h"

static const alarm_reeval_binding_t s_bindings[] = {
    { REEVAL_TRIGGER_ACTUATOR, M8_ACTUATOR_GANTRY,      M8_REEVAL_GROUP_GANTRY },
    { REEVAL_TRIGGER_ACTUATOR, M8_ACTUATOR_TOP_BRUSH,   M8_REEVAL_GROUP_TOP_BRUSH },
    { REEVAL_TRIGGER_ACTUATOR, M8_ACTUATOR_SIDE_BRUSH,  M8_REEVAL_GROUP_SIDE_BRUSH },
    { REEVAL_TRIGGER_PROCESS,  M8_CHECKPOINT_FRONT_WHEEL,     M8_REEVAL_GROUP_FRONT_WHEEL },
    { REEVAL_TRIGGER_ACTUATOR, M8_ACTUATOR_REAR_LOCK,     M8_REEVAL_GROUP_REAR_WHEEL_LOCK },
};

motion_reeval_group_id_t m8_reeval_group_lookup(reeval_trigger_kind_t kind, uint16_t trigger_id)
{
    unsigned i;

    for (i = 0U; i < (sizeof(s_bindings) / sizeof(s_bindings[0])); ++i)
    {
        if ((s_bindings[i].kind == kind) && (s_bindings[i].trigger_id == trigger_id))
        {
            return s_bindings[i].group;
        }
    }
    return ALARM_REEVAL_GROUP_NONE;
}
