/**
 * @file    operational_projection.c
 * @brief   运行模式 -> operational_snapshot 投影实现
 * @author  HUWANGWEI
 * @date    2026-07-11
 */

#include "application/operational_projection.h"

#include "common/event_types.h"
#include "common/log.h"
#include "domain/command_gateway/operational_mode.h"
#include "domain/telemetry/snapshot/operational_snapshot_internal.h"
#include "runtime/event_bus/event_bus.h"

static void sync_snapshot(void)
{
    operational_snapshot_t snap;

    snap.mode            = op_mode_get_current();
    snap.service_enabled = op_mode_is_service_enabled();
    snap.estop_active    = op_mode_is_estop_active();
    operational_snapshot_update(&snap);
}

static void on_mode_changed(const event_t *evt)
{
    (void)evt;
    sync_snapshot();
}

static void on_context_sync(const event_t *evt)
{
    (void)evt;
    sync_snapshot();
}

sw_err_t operational_projection_init(void)
{
    static const event_subscription_t s_subs[] = {
        {EVT_OP_MODE_CHANGED,      on_mode_changed},
        {EVT_OP_MODE_CONTEXT_SYNC, on_context_sync},
    };

    sw_err_t ret = event_subscribe_table(s_subs, sizeof(s_subs) / sizeof(s_subs[0]));
    if (ret != SW_OK) {
        return ret;
    }

    sync_snapshot();
    LOG_INFO("operational_projection: init ok");
    return SW_OK;
}
