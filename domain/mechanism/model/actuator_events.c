/**
 * @file    actuator_events.c
 * @brief   机构空闲生命周期事件发布
 */

#include "domain/mechanism/model/actuator_events.h"

#include "runtime/event_bus/event_bus.h"

void actuator_publish_motion_completed(actuator_id_t id)
{
    if (id == 0U) {
        return;
    }
    (void)event_publish(EVT_COMP_MOTION_COMPLETED, (uint32_t)id);
}
