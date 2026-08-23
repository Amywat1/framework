/**
 * @file    wash_events.h
 * @brief   洗车流程检查点事件（不含报警语义）
 *
 * @note    由项目编排层发布；机构模块不发布本事件。
 */
#ifndef DOMAIN_WASH_WASH_EVENTS_H
#define DOMAIN_WASH_WASH_EVENTS_H

#ifdef __cplusplus
extern "C" {
#endif

#include "common/event_types.h"

#include <stdint.h>

/** @brief 洗车流程检查点 ID（不透明，具体取值由项目定义） */
typedef uint16_t wash_checkpoint_id_t;

/**
 * @brief  发布流程检查点到达事件
 * @param  cp  项目定义的 wash_checkpoint_id_t；0 表示无效，不发布
 */
void wash_publish_checkpoint_reached(wash_checkpoint_id_t cp);

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

#endif /* DOMAIN_WASH_WASH_EVENTS_H */
