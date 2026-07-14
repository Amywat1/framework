/**
 * @file    actuator_events.h
 * @brief   机构/流程生命周期事件发布契约（不含报警语义）
 * @author  HUWANGWEI
 * @date    2026-07-10
 *
 * @note    机构与流程模块通过本接口发布 motion_completed / checkpoint_reached，
 *          由项目 wiring 层桥接到 alarm_registry_reevaluate_group()。
 */

#ifndef DOMAIN_DEVICE_CONTROL_MODEL_ACTUATOR_EVENTS_H
#define DOMAIN_DEVICE_CONTROL_MODEL_ACTUATOR_EVENTS_H

#ifdef __cplusplus
extern "C" {
#endif

#include "common/event_types.h"

#include <stdint.h>

/** @brief 执行机构 ID（不透明，具体取值由项目定义） */
typedef uint16_t actuator_id_t;

/** @brief 洗车流程检查点 ID（不透明，具体取值由项目定义） */
typedef uint16_t wash_checkpoint_id_t;

/**
 * @brief  发布机构动作完成事件
 * @param  id  项目定义的 actuator_id_t；0 表示无效，不发布
 */
void actuator_publish_motion_completed(actuator_id_t id);

/**
 * @brief  发布流程检查点到达事件
 * @param  cp  项目定义的 wash_checkpoint_id_t；0 表示无效，不发布
 */
void wash_publish_checkpoint_reached(wash_checkpoint_id_t cp);

/**
 * @brief  从 EVT_COMP_MOTION_COMPLETED 事件解包 actuator_id_t
 * @param  evt  事件指针（param 为 uint16_t actuator_id_t）
 */
static inline actuator_id_t actuator_motion_completed_id(const event_t *evt)
{
    return (actuator_id_t)(evt->param & 0xFFFFU);
}

/**
 * @brief  从 EVT_WASH_CHECKPOINT_REACHED 事件解包 wash_checkpoint_id_t
 * @param  evt  事件指针（param 为 uint16_t wash_checkpoint_id_t）
 */
static inline wash_checkpoint_id_t wash_checkpoint_reached_id(const event_t *evt)
{
    return (wash_checkpoint_id_t)(evt->param & 0xFFFFU);
}

#ifdef __cplusplus
}
#endif

#endif /* DOMAIN_DEVICE_CONTROL_MODEL_ACTUATOR_EVENTS_H */
