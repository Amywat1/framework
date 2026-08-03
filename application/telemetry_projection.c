/**
 * @file    telemetry_projection.c
 * @brief   设备遥测投影实现（运行模式、安全、洗车会话三子域合并）
 * @author  HUWANGWEI
 * @date    2026-07-14
 */

#include "application/telemetry_projection.h"

#include "common/event_types.h"
#include "common/log.h"
#include "domain/op_mode/operational_mode.h"
#include "domain/safety/alarm_registry/alarm_registry.h"
#include "domain/telemetry/device_snapshot_internal.h"
#include "ports/outbound/cloud/link/cloud_link_port.h"
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
    /* 一次持锁读出四项，避免活动表与安全姿态来自不同时刻造成快照自相矛盾 */
    snap.active_alarm_count = alarm_registry_copy_safety_view(
        snap.active_list, ALARM_ACTIVE_MAX, &snap.blocking_active, &snap.top_alarm_code, &snap.posture);
    device_snapshot_update_safety(&snap);
}

static void on_session_started(const event_t *evt)
{
    device_snapshot_set_wash_mode(wash_session_started_mode(evt->param));
}

/**
 * @brief  从云端口刷新连接状态到快照
 *
 * 领域层不依赖云端口，故由本投影承担这次读取；
 * 未注册云端口（例如仿真目标）时按未连接处理。
 */
static void sync_cloud_connected(void)
{
    const cloud_link_ops_t *ops = cloud_link_get_ops();
    bool                    online = false;

    if ((ops != NULL) && (ops->is_online != NULL)) {
        online = ops->is_online();
    }
    device_snapshot_set_cloud_connected(online);
}

static void on_cloud_link_changed(const event_t *evt)
{
    (void)evt;
    sync_cloud_connected();
}

sw_err_t telemetry_projection_init(void)
{
    static const event_subscription_t s_subs[] = {
        {EVT_OP_MODE_CHANGED,      on_mode_changed        },
        {EVT_OP_MODE_CONTEXT_SYNC, on_context_sync        },
        {EVT_ALARM_TRIGGERED,      refresh_safety_snapshot},
        {EVT_ALARM_CLEARED,        refresh_safety_snapshot},
        /* 安全姿态边沿同样要刷快照：posture 由 CRITICAL 告警驱动，
         * 但姿态事件与告警事件是两条独立发布路径，缺订阅会导致
         * LOCKOUT/NOMINAL 切换后快照里的 posture 滞后。 */
        {EVT_SAFETY_LOCKOUT,       refresh_safety_snapshot},
        {EVT_SAFETY_NOMINAL,       refresh_safety_snapshot},
        {EVT_WASH_SESSION_STARTED, on_session_started     },
        /* 云连接状态并入同一快照，使读侧（CLI / 诊断 / 状态上报）看到的
         * 连接状态与运行模式、安全状态来自同一时刻。 */
        {EVT_CLOUD_CONNECTED,      on_cloud_link_changed  },
        {EVT_CLOUD_DISCONNECTED,   on_cloud_link_changed  },
    };

    sw_err_t ret = event_subscribe_table(s_subs, sizeof(s_subs) / sizeof(s_subs[0]));
    if (ret != SW_OK) {
        return ret;
    }

    sync_op_snapshot();
    refresh_safety_snapshot(NULL);
    sync_cloud_connected();
    LOG_INFO("telemetry_projection: init ok");
    return SW_OK;
}
