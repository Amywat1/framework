/**
 * @file    alarm_event_bridge.c
 * @brief   报警域事件桥接实现
 * @author  HUWANGWEI
 * @date    2026-07-09
 */

#include "application/alarm_event_bridge.h"

#include "common/event_types.h"
#include "domain/safety/alarm_registry/alarm_registry.h"
#include "domain/safety/model/alarm_types.h"
#include "runtime/event_bus/event_bus.h"
#include "runtime/scheduler/periodic_task.h"
#include "runtime/config/thread_config.h"

#include <sched.h>

static void alarm_event_bridge_drain(void)
{
    alarm_domain_event_t batch[ALARM_PENDING_EVENT_MAX];
    unsigned             n;
    unsigned             i;

    do {
        n = alarm_registry_pull_events(batch, ALARM_PENDING_EVENT_MAX);
        for (i = 0U; i < n; ++i) {
            switch (batch[i].kind) {
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

static void bridge_tick(void *ctx)
{
    (void)ctx;
    alarm_event_bridge_drain();
}

sw_err_t alarm_event_bridge_init(void)
{
    return periodic_task_register("alarm_bridge", 50U, bridge_tick, NULL,
                                  SCHED_OTHER, 0, THD_SENSOR_POLL_STACK);
}
