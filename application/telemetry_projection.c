/**
 * @file    telemetry_projection.c
 * @brief   设备遥测投影实现（运行模式、安全、洗车会话三子域合并）
 * @author  HUWANGWEI
 * @date    2026-07-14
 */

#include "application/telemetry_projection.h"

#include "application/job_admit.h"
#include "application/wash_quota.h"
#include "application/ports/outbound/cloud/link/cloud_link_port.h"
#include "common/event_types.h"
#include "common/log.h"
#include "domain/job/active_jobs.h"
#include "domain/job/job_types.h"
#include "domain/job/machine_profile.h"
#include "domain/job/order_book.h"
#include "domain/op_mode/operational_mode.h"
#include "domain/ports/outbound/safety/safety_port.h"
#include "domain/safety/alarm_registry/alarm_registry.h"
#include "domain/telemetry/device_snapshot_internal.h"
#include "runtime/config/thread_config.h"
#include "runtime/event_bus/event_bus.h"
#include "runtime/scheduler/periodic_task.h"

#include <sched.h>
#include <string.h>

#define TELEMETRY_PROJECTION_RECONCILE_PERIOD_MS 1000U

static bool     s_reconcile_registered;
static uint32_t s_last_dropped_count;

static void sync_op_snapshot(const event_t *evt)
{
    operational_snapshot_t snap;

    (void)evt;
    op_mode_copy_view(&snap.mode, &snap.service_enabled, &snap.estop_active);
    device_snapshot_update_op(&snap);
}

static void refresh_safety_snapshot(const event_t *evt)
{
    safety_snapshot_t snap;

    (void)evt;
    memset(&snap, 0, sizeof(snap));
    alarm_safety_view_t view;

    memset(&view, 0, sizeof(view));
    (void)alarm_registry_copy_safety_view(&view);
    snap.active_alarm_count = view.count;
    if (view.count > 0U) {
        memcpy(snap.active_list, view.list, view.count * sizeof(snap.active_list[0]));
    }
    snap.blocking_active         = view.blocking;
    snap.top_alarm_code          = view.top_code;
    snap.posture                 = view.posture;
    snap.session_journal_count   = view.journal_count;
    snap.session_journal_dropped = view.journal_dropped;
    if (view.journal_count > 0U) {
        memcpy(snap.session_journal, view.session_journal, view.journal_count * sizeof(snap.session_journal[0]));
    }
    snap.cutout_unconfirmed = safety_cutout_is_unconfirmed();
    if (snap.cutout_unconfirmed) {
        snap.posture         = SAFETY_POSTURE_LOCKOUT;
        snap.blocking_active = true;
    }
    device_snapshot_update_safety(&snap);
}

static unsigned sync_job_snapshot(void)
{
    wash_snapshot_t snap;
    job_desc_t      head;

    memset(&snap, 0, sizeof(snap));
    snap.queued_count    = order_book_count();
    snap.active_count    = active_jobs_copy(snap.active, JOB_ACTIVE_CAP);
    snap.max_active_jobs = machine_profile_get().max_active_jobs;
    if (order_book_peek_front(&head) == SW_OK) {
        snap.head_order_id = head.order_id;
        snap.head_mode     = head.mode;
    }
    if (snap.active_count > 0U) {
        snap.mode = snap.active[0].mode;
    } else if (snap.queued_count > 0U) {
        snap.mode = snap.head_mode;
    }
    device_snapshot_update_job(&snap);
    return snap.active_count;
}

static void on_job_started(const event_t *evt)
{
    if (sync_job_snapshot() == 0U) {
        device_snapshot_set_wash_mode(job_started_mode(evt->param));
    }
}

static void on_job_occupancy_changed(const event_t *evt)
{
    (void)evt;
    (void)sync_job_snapshot();
}

/**
 * @brief  从云端口刷新连接状态到快照
 *
 * 领域层不依赖云端口，故由本投影承担这次读取；
 * 未注册云端口（例如仿真目标）时按未连接处理。
 */
static void sync_cloud_connected(const event_t *evt)
{
    const cloud_link_ops_t *ops    = cloud_link_get_ops();
    bool                    online = false;

    (void)evt;
    if ((ops != NULL) && (ops->is_online != NULL)) {
        online = ops->is_online();
    }
    /* 连通视为可监控；只改标志，次数授权模块不会因此回满。 */
    wash_quota_set_monitorable(online);
    device_snapshot_set_cloud_connected(online);
}

static void sync_wash_quota(void)
{
    wash_quota_info_t info = {0};

    wash_quota_get_info(&info);
    device_snapshot_set_wash_quota(info.monitorable, info.remaining, info.grant_serial);
}

void telemetry_projection_sync_all(void)
{
    sync_op_snapshot(NULL);
    refresh_safety_snapshot(NULL);
    sync_cloud_connected(NULL);
    sync_wash_quota();
    (void)sync_job_snapshot();
}

static void reconcile_tick(void *ctx)
{
    event_bus_stats_t stats;

    (void)ctx;
    job_admit_expire();
    if (event_bus_get_stats(&stats) != SW_OK) {
        LOG_WARN("telemetry_projection: unable to read event bus stats");
        return;
    }

    /* 周期重建同时提供固定滞后上限；统计变化只用于诊断，不能替代周期重建，
     * 因为事件可能在订阅前、初始化间隙或非投影订阅者路径中丢失。 */
    if (stats.dropped_count != s_last_dropped_count) {
        LOG_WARN("telemetry_projection: event loss detected dropped=%u last=%u, rebuilding",
                 (unsigned)stats.dropped_count,
                 (unsigned)s_last_dropped_count);
        s_last_dropped_count = stats.dropped_count;
    }
    telemetry_projection_sync_all();
}

sw_err_t telemetry_projection_init(void)
{
    static const event_subscription_t s_subs[] = {
        {EVT_OP_MODE_CHANGED,      sync_op_snapshot       },
        {EVT_OP_MODE_CONTEXT_SYNC, sync_op_snapshot       },
        {EVT_ALARM_TRIGGERED,      refresh_safety_snapshot},
        {EVT_ALARM_CLEARED,        refresh_safety_snapshot},
        /* 安全姿态边沿同样要刷快照：posture 由 CRITICAL 告警驱动，
         * 但姿态事件与告警事件是两条独立发布路径，缺订阅会导致
         * LOCKOUT/NOMINAL 切换后快照里的 posture 滞后。 */
        {EVT_SAFETY_LOCKOUT,       refresh_safety_snapshot},
        {EVT_SAFETY_NOMINAL,       refresh_safety_snapshot},
        {EVT_HW_ESTOP_ON,          refresh_safety_snapshot},
        {EVT_JOB_STARTED,          on_job_started           },
        {EVT_JOB_COMPLETED,        on_job_occupancy_changed },
        {EVT_JOB_ABORTED,          on_job_occupancy_changed },
        /* 云连接状态并入同一快照，使读侧（CLI / 诊断 / 状态上报）看到的
         * 连接状态与运行模式、安全状态来自同一时刻。 */
        {EVT_CLOUD_CONNECTED,      sync_cloud_connected   },
        {EVT_CLOUD_DISCONNECTED,   sync_cloud_connected   },
    };

    sw_err_t ret = event_subscribe_table(s_subs, sizeof(s_subs) / sizeof(s_subs[0]));
    if (ret != SW_OK) {
        return ret;
    }

    telemetry_projection_sync_all();
    if (!s_reconcile_registered) {
        sw_err_t task_ret = periodic_task_register("telemetry_projection",
                                                   TELEMETRY_PROJECTION_RECONCILE_PERIOD_MS,
                                                   reconcile_tick,
                                                   NULL,
                                                   SCHED_OTHER,
                                                   0,
                                                   THD_TELEMETRY_STACK);
        if (task_ret != SW_OK) {
            return task_ret;
        }
        s_reconcile_registered = true;
    }
    {
        event_bus_stats_t stats;

        s_last_dropped_count = 0U;
        if (event_bus_get_stats(&stats) == SW_OK) {
            s_last_dropped_count = stats.dropped_count;
        }
    }
    LOG_INFO("telemetry_projection: init ok");
    return SW_OK;
}
