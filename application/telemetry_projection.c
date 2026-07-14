/**
 * @file    telemetry_projection.c
 * @brief   设备遥测投影实现（运行模式、安全、洗车会话三子域合并）
 * @author  HUWANGWEI
 * @date    2026-07-14
 */

#include "application/telemetry_projection.h"

#include "common/event_types.h"
#include "common/log.h"
#include "domain/command_gateway/operational_mode.h"
#include "domain/safety/alarm_registry/alarm_registry.h"
#include "domain/telemetry/device_snapshot_internal.h"
#include "domain/wash/model/wash_types.h"
#include "runtime/event_bus/event_bus.h"

static void sync_op_snapshot(void)
{
    operational_snapshot_t snap;

    snap.mode            = op_mode_get_current();
    snap.service_enabled = op_mode_is_service_enabled();
    snap.estop_active    = op_mode_is_estop_active();
    device_snapshot_update_op(&snap);
}

static void on_mode_changed(const event_t *evt)
{
    (void)evt;
    sync_op_snapshot();
}

static void on_context_sync(const event_t *evt)
{
    (void)evt;
    sync_op_snapshot();
}

static void refresh_safety_snapshot(const event_t *evt)
{
    safety_snapshot_t snap;

    (void)evt;
    snap.active_alarm_count = alarm_registry_copy_active_projection(
        snap.active_list, ALARM_ACTIVE_MAX, &snap.blocking_active, &snap.top_alarm_code);
    snap.posture = alarm_registry_safety_posture();
    device_snapshot_update_safety(&snap);
}

static void on_session_started(const event_t *evt)
{
    device_snapshot_set_wash_mode((wash_mode_t)(evt->param & 0xFFU));
}

sw_err_t telemetry_projection_init(void)
{
    static const event_subscription_t s_subs[] = {
        {EVT_OP_MODE_CHANGED,      on_mode_changed},
        {EVT_OP_MODE_CONTEXT_SYNC, on_context_sync},
        {EVT_ALARM_TRIGGERED,      refresh_safety_snapshot},
        {EVT_ALARM_CLEARED,        refresh_safety_snapshot},
        {EVT_ALARM_BATCH_CLEARED,  refresh_safety_snapshot},
        {EVT_WASH_SESSION_STARTED, on_session_started},
    };

    sw_err_t ret = event_subscribe_table(s_subs, sizeof(s_subs) / sizeof(s_subs[0]));
    if (ret != SW_OK) {
        return ret;
    }

    sync_op_snapshot();
    refresh_safety_snapshot(NULL);
    LOG_INFO("telemetry_projection: init ok");
    return SW_OK;
}
