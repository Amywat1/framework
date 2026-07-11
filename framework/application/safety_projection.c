/**
 * @file    safety_projection.c
 * @brief   报警/安全 → safety_snapshot 投影实现
 * @author  HUWANGWEI
 * @date    2026-07-11
 */

#include "framework/application/safety_projection.h"
#include "framework/domain/safety/alarm_registry/alarm_registry.h"
#include "framework/domain/telemetry/snapshot/safety_snapshot_internal.h"
#include "framework/common/event_types.h"
#include "framework/runtime/event_bus/event_bus.h"
#include "framework/common/log.h"

static void refresh_snapshot(const event_t *evt)
{
    safety_snapshot_t snap;

    (void)evt;

    snap.active_alarm_count = alarm_registry_copy_active_projection(
        snap.active_list,
        ALARM_ACTIVE_MAX,
        &snap.blocking_active,
        &snap.top_alarm_code);
    snap.posture = alarm_registry_safety_posture();
    safety_snapshot_update(&snap);
}

sw_err_t safety_projection_init(void)
{
    static const event_subscription_t s_subs[] = {
        { EVT_ALARM_TRIGGERED,     refresh_snapshot },
        { EVT_ALARM_CLEARED,       refresh_snapshot },
        { EVT_ALARM_BATCH_CLEARED, refresh_snapshot },
    };

    sw_err_t ret = event_subscribe_table(s_subs, sizeof(s_subs) / sizeof(s_subs[0]));
    if (ret != SW_OK)
    {
        return ret;
    }

    refresh_snapshot(NULL);
    LOG_INFO("safety_projection: init ok");
    return SW_OK;
}
