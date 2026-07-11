/**
 * @file    m8_actuator_ids.h
 * @brief   M8 执行机构与流程检查点 ID
 * @author  HUWANGWEI
 * @date    2026-07-10
 */

#ifndef CONFIG_M8_ACTUATOR_IDS_H
#define CONFIG_M8_ACTUATOR_IDS_H

#include "framework/domain/device_control/model/actuator_events.h"

typedef enum
{
    M8_ACTUATOR_NONE = 0,
    M8_ACTUATOR_GANTRY,
    M8_ACTUATOR_TOP_BRUSH,
    M8_ACTUATOR_SIDE_BRUSH,
    M8_ACTUATOR_FAN,
    M8_ACTUATOR_REAR_LOCK,
    M8_ACTUATOR_WATER,
} m8_actuator_id_t;

typedef enum
{
    M8_CHECKPOINT_NONE = 0,
    M8_CHECKPOINT_FRONT_WHEEL,
    M8_CHECKPOINT_REAR_WHEEL_LOCK,
} m8_wash_checkpoint_id_t;

#endif /* CONFIG_M8_ACTUATOR_IDS_H */
