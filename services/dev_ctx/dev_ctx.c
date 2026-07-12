/**
 * @file    dev_ctx.c
 * @brief   设备状态快照实现（组合分域读模型）
 * @author  HUWANGWEI
 * @date    2026-07-09
 */

#include "services/dev_ctx/dev_ctx.h"

#include "common/log.h"
#include "domain/telemetry/snapshot/operational_snapshot.h"
#include "domain/telemetry/snapshot/safety_snapshot.h"
#include "domain/telemetry/snapshot/wash_snapshot.h"
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
    device_context_t       out;
    operational_snapshot_t op_snap;
    safety_snapshot_t      safety_snap;
    wash_snapshot_t        wash_snap;

    op_snap     = operational_snapshot_get();
    safety_snap = safety_snapshot_get();
    wash_snap   = wash_snapshot_get();

    out.operational_mode   = op_snap.mode;
    out.service_enabled    = op_snap.service_enabled;
    out.estop_active       = op_snap.estop_active;
    out.wash_mode          = wash_snap.mode;
    out.safety_posture     = safety_snap.posture;
    out.blocking_active    = safety_snap.blocking_active;
    out.top_alarm_code     = safety_snap.top_alarm_code;
    out.active_alarm_count = safety_snap.active_alarm_count;
    memcpy(out.active_list, safety_snap.active_list, sizeof(out.active_list));

    out.cloud_connected = read_cloud_connected();
    return out;
}

operational_mode_t dev_ctx_get_operational_mode(void)
{
    return operational_snapshot_get().mode;
}
