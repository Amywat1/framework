/**
 * @file    safety_posture.c
 * @brief   安全姿态两态聚合实现
 * @author  HUWANGWEI
 * @date    2026-07-09
 */

#include "domain/safety/safety_posture/safety_posture.h"

#include "common/event_types.h"
#include "common/log.h"
#include "domain/safety/alarm_registry/alarm_registry.h"
#include "domain/safety/model/safety_types.h"
#include "runtime/event_bus/event_bus.h"

static safety_posture_t s_posture = SAFETY_POSTURE_NOMINAL;

static void recompute_posture(const event_t *evt)
{
    (void)evt;

    safety_posture_t next = alarm_registry_safety_posture();

    if (next == s_posture) {
        return;
    }

    s_posture = next;
    if (next == SAFETY_POSTURE_LOCKOUT) {
        LOG_WARN("safety_posture: -> LOCKOUT");
        (void)event_publish(EVT_SAFETY_LOCKOUT, 0U);
    } else {
        LOG_INFO("safety_posture: -> NOMINAL");
        (void)event_publish(EVT_SAFETY_NOMINAL, 0U);
    }
}

sw_err_t safety_posture_init(void)
{
    static const event_subscription_t s_subs[] = {
        {EVT_ALARM_TRIGGERED,     recompute_posture},
        {EVT_ALARM_CLEARED,       recompute_posture},
        {EVT_ALARM_BATCH_CLEARED, recompute_posture},
    };

    s_posture = SAFETY_POSTURE_NOMINAL;

    return event_subscribe_table(s_subs, sizeof(s_subs) / sizeof(s_subs[0]));
}
