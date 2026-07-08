/**
 * @file    report_aggregator.c
 * @brief   云端状态上报聚合器实现
 * @author  HUWANGWEI
 * @date    2026-04-10
 */

#include "framework/application/orchestrators/report_aggregator.h"
#include "framework/runtime/scheduler/thread_registry.h"
#include "framework/ports/outbound/cloud/connection/connection_port.h"
#include "framework/ports/outbound/cloud/report/report_port.h"
#include "framework/runtime/event_bus/event_bus.h"
#include "framework/runtime/config/thread_config.h"
#include "framework/common/event_types.h"
#include "framework/common/log.h"
#include <unistd.h>
#include <sched.h>

static const char *const s_alarm_report_ids[] = {
    "sts_dev_warning",
    "sts_emergency",
};

static bool cloud_is_connected(void)
{
    const cloud_connection_ops_t *ops = cloud_connection_get_ops();

    if ((ops == NULL) || (ops->is_connected == NULL))
    {
        return false;
    }
    return ops->is_connected();
}

static void report_properties_once(void)
{
    const cloud_report_ops_t *ops = cloud_report_get_ops();

    if (!cloud_is_connected() || (ops == NULL) || (ops->publish_properties == NULL))
    {
        return;
    }

    sw_err_t ret = ops->publish_properties();
    if ((ret != SW_OK) && (ret != SW_ERR_COMM))
    {
        LOG_WARN("report_aggregator: publish_properties failed ret=%d", (int)ret);
    }
}

static void report_alarm_delta_once(void)
{
    const cloud_report_ops_t *ops = cloud_report_get_ops();

    if (!cloud_is_connected() || (ops == NULL) || (ops->publish_properties_delta == NULL))
    {
        return;
    }

    sw_err_t ret = ops->publish_properties_delta(s_alarm_report_ids,
                                                  sizeof(s_alarm_report_ids) /
                                                  sizeof(s_alarm_report_ids[0]));
    if ((ret != SW_OK) && (ret != SW_ERR_COMM))
    {
        LOG_WARN("report_aggregator: publish_properties_delta failed ret=%d", (int)ret);
    }
}

static void *cloud_thread_fn(void *arg)
{
    (void)arg;

    while (true)
    {
        usleep((unsigned long)THD_CLOUD_REPORT_PERIOD_MS * 1000UL);
        report_properties_once();
    }

    return NULL;
}

void report_aggregator_request_resync(void)
{
    report_properties_once();
}

static void on_cloud_connected(const event_t *evt)
{
    (void)evt;
    LOG_INFO("report_aggregator: cloud connected, resync");
    report_properties_once();
}

static void on_alarm_changed(const event_t *evt)
{
    (void)evt;
    report_alarm_delta_once();
}

sw_err_t report_aggregator_init(void)
{
    static const event_subscription_t s_subs[] = {
        { EVT_CLOUD_CONNECTED,  on_cloud_connected },
        { EVT_ALARM_TRIGGERED,  on_alarm_changed   },
        { EVT_ALARM_CLEARED,    on_alarm_changed   },
    };
    sw_err_t ret;

    ret = event_subscribe_table(s_subs, sizeof(s_subs) / sizeof(s_subs[0]));
    if (ret != SW_OK)
    {
        return ret;
    }

    ret = thread_register("cloud", cloud_thread_fn,
                          SCHED_OTHER, 0, THD_CLOUD_STACK);
    if (ret != SW_OK)
    {
        return ret;
    }

    LOG_INFO("report_aggregator: init ok");
    return SW_OK;
}
