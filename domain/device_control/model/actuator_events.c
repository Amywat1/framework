/**
 * @file    actuator_events.c
 * @brief   机构/流程生命周期事件发布实现
 * @author  HUWANGWEI
 * @date    2026-07-10
 */

#include "domain/device_control/model/actuator_events.h"

#include "common/event_types.h"
#include "runtime/event_bus/event_bus.h"

void actuator_publish_motion_completed(actuator_id_t id)
{
    if (id == 0U) {
        return;
    }
    (void)event_publish(EVT_COMP_MOTION_COMPLETED, (uint32_t)id);
}

void wash_publish_checkpoint_reached(wash_checkpoint_id_t cp)
{
    if (cp == 0U) {
        return;
    }
    (void)event_publish(EVT_WASH_CHECKPOINT_REACHED, (uint32_t)cp);
}
