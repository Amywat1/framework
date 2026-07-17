/**
 * @file    alarm_event_bridge.c
 * @brief   报警域事件桥接（pending → EVT_ALARM_*，并边沿发布 EVT_SAFETY_*）
 * @author  HUWANGWEI
 * @date    2026-07-09
 */

#include "adapters/inbound/event/alarm_event_bridge.h"

#include "common/event_types.h"
#include "common/log.h"
#include "domain/safety/alarm_registry/alarm_registry.h"
#include "domain/safety/model/alarm_types.h"
#include "runtime/config/thread_config.h"
#include "runtime/event_bus/event_bus.h"
#include "runtime/scheduler/periodic_task.h"

#include <sched.h>

static safety_posture_t s_posture = SAFETY_POSTURE_NOMINAL;

static void publish_posture_edge(void)
{
    safety_posture_t next = alarm_registry_safety_posture();

    if (next == s_posture) {
        return;
    }

    s_posture = next;
    if (next == SAFETY_POSTURE_LOCKOUT) {
        LOG_WARN("alarm_event_bridge: posture -> LOCKOUT");
        (void)event_publish(EVT_SAFETY_LOCKOUT, 0U);
    } else {
        LOG_INFO("alarm_event_bridge: posture -> NOMINAL");
        (void)event_publish(EVT_SAFETY_NOMINAL, 0U);
    }
}

void alarm_event_bridge_drain(void)
{
    alarm_domain_event_t batch[ALARM_PENDING_EVENT_MAX];
    unsigned             n;
    unsigned             i;
    bool                 any = false;

    do {
        n = alarm_registry_pull_events(batch, ALARM_PENDING_EVENT_MAX);
        for (i = 0U; i < n; ++i) {
            any = true;
            switch (batch[i].kind) {
            case ALARM_DOMAIN_EVT_TRIGGERED:
                (void)event_publish(EVT_ALARM_TRIGGERED, batch[i].code);
                break;
            case ALARM_DOMAIN_EVT_CLEARED:
                (void)event_publish(EVT_ALARM_CLEARED, batch[i].code);
                break;
            default:
                break;
            }
        }
    } while (n == ALARM_PENDING_EVENT_MAX);

    if (any) {
        publish_posture_edge();
    }
}

static void bridge_tick(void *ctx)
{
    (void)ctx;
    alarm_event_bridge_drain();
}

sw_err_t alarm_event_bridge_init(void)
{
    s_posture = SAFETY_POSTURE_NOMINAL;
    return periodic_task_register("alarm_bridge", 50U, bridge_tick, NULL,
                                  SCHED_OTHER, 0, THD_SENSOR_POLL_STACK);
}
