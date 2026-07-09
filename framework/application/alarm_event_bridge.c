/**
 * @file    alarm_event_bridge.c
 * @brief   报警域事件桥接实现
 * @author  HUWANGWEI
 * @date    2026-07-09
 */

#include "framework/application/alarm_event_bridge.h"
#include "framework/domain/safety/alarm_registry/alarm_registry.h"
#include "framework/domain/safety/model/alarm_types.h"
#include "framework/runtime/event_bus/event_bus.h"
#include "framework/common/event_types.h"

sw_err_t alarm_event_bridge_init(void)
{
    return SW_OK;
}

void alarm_event_bridge_drain(void)
{
    alarm_domain_event_t batch[ALARM_PENDING_EVENT_MAX];
    unsigned             n;
    unsigned             i;

    do
    {
        n = alarm_registry_pull_events(batch, ALARM_PENDING_EVENT_MAX);
        for (i = 0U; i < n; ++i)
        {
            switch (batch[i].kind)
            {
            case ALARM_DOMAIN_EVT_TRIGGERED:
                (void)event_publish(EVT_ALARM_TRIGGERED, batch[i].code);
                break;
            case ALARM_DOMAIN_EVT_CLEARED:
                (void)event_publish(EVT_ALARM_CLEARED, batch[i].code);
                break;
            case ALARM_DOMAIN_EVT_BATCH_CLEARED:
                (void)event_publish(EVT_ALARM_BATCH_CLEARED, batch[i].code);
                break;
            default:
                break;
            }
        }
    } while (n == ALARM_PENDING_EVENT_MAX);
}
