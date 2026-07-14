/**
 * @file    dev_ctx.c
 * @brief   设备状态快照实现（组合分域读模型）
 * @author  HUWANGWEI
 * @date    2026-07-09
 */

#include "services/dev_ctx/dev_ctx.h"

#include "common/log.h"
#include "domain/telemetry/device_snapshot.h"
#include "ports/outbound/cloud/link/cloud_link_port.h"

#include <string.h>

static bool read_cloud_connected(void)
{
    const cloud_link_ops_t *ops = cloud_link_get_ops();

    if ((ops == NULL) || (ops->is_online == NULL)) {
        return false;
    }
    return ops->is_online();
}

sw_err_t dev_ctx_init(void)
{
    LOG_INFO("dev_ctx: init ok");
    return SW_OK;
}

device_context_t dev_ctx_snapshot(void)
{
    device_context_t  out;
    device_snapshot_t snap;

    snap = device_snapshot_get();

    out.operational_mode   = snap.op.mode;
    out.service_enabled    = snap.op.service_enabled;
    out.estop_active       = snap.op.estop_active;
    out.wash_mode          = snap.wash.mode;
    out.safety_posture     = snap.safety.posture;
    out.blocking_active    = snap.safety.blocking_active;
    out.top_alarm_code     = snap.safety.top_alarm_code;
    out.active_alarm_count = snap.safety.active_alarm_count;
    memcpy(out.active_list, snap.safety.active_list, sizeof(out.active_list));

    out.cloud_connected = read_cloud_connected();
    return out;
}

operational_mode_t dev_ctx_get_operational_mode(void)
{
    return device_snapshot_get().op.mode;
}
