/**
 * @file    m8_alarm_reeval_bindings.h
 * @brief   M8 ON_MOTION 触发源 → 重评估分组映射
 * @author  HUWANGWEI
 * @date    2026-07-10
 */

#ifndef M8_ALARM_REEVAL_BINDINGS_H
#define M8_ALARM_REEVAL_BINDINGS_H

#ifdef __cplusplus
extern "C" {
#endif

#include "framework/domain/safety/model/alarm_types.h"
#include "projects/m8/config/m8_actuator_ids.h"

typedef enum
{
    REEVAL_TRIGGER_ACTUATOR = 0,
    REEVAL_TRIGGER_PROCESS,
} reeval_trigger_kind_t;

typedef struct
{
    reeval_trigger_kind_t     kind;
    uint16_t                  trigger_id;
    motion_reeval_group_id_t  group;
} alarm_reeval_binding_t;

/**
 * @brief  按触发源查找重评估分组
 * @param  kind        触发源类型
 * @param  trigger_id  actuator_id 或 checkpoint_id
 * @return 匹配的分组 ID；未命中返回 ALARM_REEVAL_GROUP_NONE
 */
motion_reeval_group_id_t m8_reeval_group_lookup(reeval_trigger_kind_t kind, uint16_t trigger_id);

#ifdef __cplusplus
}
#endif

#endif /* M8_ALARM_REEVAL_BINDINGS_H */
