/**
 * @file    wash_events.c
 * @brief   洗车流程检查点事件发布
 */

#include "domain/wash/wash_events.h"

#include "runtime/event_bus/event_bus.h"

void wash_publish_checkpoint_reached(wash_checkpoint_id_t cp)
{
    if (cp == 0U) {
        return;
    }
    (void)event_publish(EVT_WASH_CHECKPOINT_REACHED, (uint32_t)cp);
}
