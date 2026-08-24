/**
 * @file    report_scheduler.c
 * @brief   云端状态上报调度器实现
 * @author  HUWANGWEI
 * @date    2026-07-08
 */

#include "application/orchestrators/report_scheduler.h"

#include "application/ports/outbound/cloud/link/cloud_link_port.h"
#include "common/event_types.h"
#include "common/log.h"
#include "common/sw_error.h"
#include "domain/cloud/cloud_model.h"
#include "domain/cloud/cloud_point_watcher.h"
#include "runtime/config/thread_config.h"
#include "runtime/event_bus/event_bus.h"
#include "runtime/scheduler/periodic_task.h"

#include <sched.h>
#include <stdbool.h>

static uint32_t s_poll_ms        = 0U;
static uint32_t s_full_period_ms = 0U;
static uint32_t s_elapsed_ms     = 0U;
static bool     s_started        = false;

static bool cloud_is_online(void)
{
    const cloud_link_ops_t *ops = cloud_link_get_ops();

    if ((ops == NULL) || (ops->is_online == NULL)) {
        return false;
    }
    return ops->is_online();
}

static void link_poll_if_needed(void)
{
    const cloud_link_ops_t *ops = cloud_link_get_ops();

    if ((ops != NULL) && (ops->poll != NULL)) {
        ops->poll();
    }
}

static void report_properties_once(void)
{
    const cloud_link_ops_t *ops = cloud_link_get_ops();

    if (!cloud_is_online() || (ops == NULL) || (ops->publish_properties == NULL)) {
        return;
    }

    sw_err_t ret = ops->publish_properties();

    if ((ret != SW_OK) && !sw_err_is_transient(ret)) {
        LOG_WARN("report_scheduler: publish_properties failed ret=%s", sw_err_name(ret));
    }
}

static void report_dirty_batch(void)
{
    const cloud_link_ops_t *ops = cloud_link_get_ops();
    const char             *ids[CLOUD_POINT_TABLE_MAX];
    size_t                  n;

    if (!cloud_is_online() || (ops == NULL) || (ops->publish_properties_delta == NULL)) {
        return;
    }

    n = cloud_point_watcher_take_dirty(ids, CLOUD_POINT_TABLE_MAX);
    if (n == 0U) {
        return;
    }

    sw_err_t ret = ops->publish_properties_delta(ids, n);

    if (ret != SW_OK) {
        cloud_point_watcher_restore_dirty(ids, n);
        if (!sw_err_is_transient(ret)) {
            LOG_WARN("report_scheduler: publish_properties_delta failed ret=%s", sw_err_name(ret));
        }
    }
}

void report_scheduler_poll(void)
{
    link_poll_if_needed();
    cloud_point_watcher_poll();
    report_dirty_batch();

    if (s_full_period_ms == 0U) {
        return;
    }

    s_elapsed_ms += s_poll_ms;
    if (s_elapsed_ms >= s_full_period_ms) {
        s_elapsed_ms = 0U;
        report_properties_once();
    }
}

static void periodic_cb(void *ctx)
{
    (void)ctx;
    report_scheduler_poll();
}

static void on_cloud_connected(const event_t *evt)
{
    (void)evt;
    LOG_INFO("report_scheduler: cloud connected, resync");
    report_properties_once();
}

void report_scheduler_reset_for_test(void)
{
    s_poll_ms        = 0U;
    s_full_period_ms = 0U;
    s_elapsed_ms     = 0U;
    s_started        = false;
}

sw_err_t report_scheduler_start(uint32_t poll_ms, uint32_t full_period_ms)
{
    size_t                     count   = 0U;
    const cloud_point_entry_t *entries = cloud_model_entries(&count);
    sw_err_t                   ret;

    if (s_started) {
        return SW_OK;
    }

    if (poll_ms == 0U) {
        return SW_ERR_PARAM;
    }
    if ((full_period_ms != 0U) && ((full_period_ms % poll_ms) != 0U)) {
        LOG_ERROR("report_scheduler: full_period_ms 必须是 poll_ms 的整数倍");
        return SW_ERR_PARAM;
    }
    if ((entries == NULL) || (count == 0U)) {
        return SW_ERR_NOT_INIT;
    }

    ret = cloud_point_watcher_init(entries, count);
    if (ret != SW_OK) {
        return ret;
    }

    ret = event_subscribe(EVT_CLOUD_CONNECTED, on_cloud_connected);
    if (ret != SW_OK) {
        return ret;
    }

    ret = periodic_task_register("cloud_report", poll_ms, periodic_cb, NULL, SCHED_OTHER, 0, THD_CLOUD_STACK);
    if (ret != SW_OK) {
        return ret;
    }

    s_poll_ms        = poll_ms;
    s_full_period_ms = full_period_ms;
    s_elapsed_ms     = 0U;
    s_started        = true;
    LOG_INFO("report_scheduler: started poll=%ums full=%ums", (unsigned)poll_ms, (unsigned)full_period_ms);
    return SW_OK;
}
