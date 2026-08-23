/**
 * @file    actuator_events.h
 * @brief   机构空闲生命周期事件发布契约（不含报警语义）
 *
 * @note    由 `motor_axis_poll()` 在轴进入 IDLE 时发布；项目 wiring 可订阅后
 *          做 ON_MOTION 重评估。运动结局走 `on_motion_end`，不走本事件。
 */
#ifndef DOMAIN_MECHANISM_MODEL_ACTUATOR_EVENTS_H
#define DOMAIN_MECHANISM_MODEL_ACTUATOR_EVENTS_H

#ifdef __cplusplus
extern "C" {
#endif

#include "common/event_types.h"

#include <stdint.h>

/** @brief 执行机构 ID（不透明，具体取值由项目定义） */
typedef uint16_t actuator_id_t;

/**
 * @brief  发布机构动作完成事件
 * @param  id  项目定义的 actuator_id_t；0 表示无效，不发布
 */
void actuator_publish_motion_completed(actuator_id_t id);

/**
 * @brief  从 EVT_COMP_MOTION_COMPLETED 事件解包 actuator_id_t
 * @param  evt  事件指针（param 为 uint16_t actuator_id_t）
 */
static inline actuator_id_t actuator_motion_completed_id(const event_t *evt)
{
    return (actuator_id_t)(evt->param & 0xFFFFU);
}

#ifdef __cplusplus
}
#endif

#endif /* DOMAIN_MECHANISM_MODEL_ACTUATOR_EVENTS_H */
