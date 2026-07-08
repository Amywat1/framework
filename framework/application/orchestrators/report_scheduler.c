/**
 * @file    report_scheduler.c
 * @brief   云端状态上报调度器实现
 * @author  HUWANGWEI
 * @date    2026-07-08
 */

#include "framework/application/orchestrators/report_scheduler.h"
#include "framework/cloud/cloud_point_watcher.h"
#include "framework/runtime/scheduler/periodic_task.h"
#include "framework/ports/outbound/cloud/link/cloud_link_port.h"
#include "framework/ports/outbound/cloud/report/report_port.h"
#include "framework/runtime/event_bus/event_bus.h"
#include "framework/runtime/config/thread_config.h"
#include "framework/common/event_types.h"
#include "framework/common/log.h"
#include <sched.h>
#include <string.h>

#define REPORT_POLICY_MAX   8U

typedef struct
{
    report_policy_entry_t entry;
} report_policy_slot_t;

static report_policy_slot_t           s_policies[REPORT_POLICY_MAX];
static size_t                         s_policy_count = 0U;
static uint32_t                       s_period_ms    = 0U;
static bool                           s_started      = false;
static report_point_id_resolver_fn_t  s_point_resolver = NULL;

void report_scheduler_register_point_resolver(report_point_id_resolver_fn_t resolver)
{
    s_point_resolver = resolver;
}

static bool cloud_is_online(void)
{
    const cloud_link_ops_t *ops = cloud_link_get_ops();

    if ((ops == NULL) || (ops->is_online == NULL))
    {
        return false;
    }
    return ops->is_online();
}

static void link_poll_if_needed(void)
{
    const cloud_link_ops_t *ops = cloud_link_get_ops();

    if ((ops != NULL) && (ops->poll != NULL))
    {
        ops->poll();
    }
}

static void report_properties_once(void)
{
    const cloud_report_ops_t *ops = cloud_report_get_ops();

    if (!cloud_is_online() || (ops == NULL) || (ops->publish_properties == NULL))
    {
        return;
    }

    sw_err_t ret = ops->publish_properties();
    if ((ret != SW_OK) && (ret != SW_ERR_COMM))
    {
        LOG_WARN("report_scheduler: publish_properties failed ret=%d", (int)ret);
    }
}

static void report_delta_once(const char *const *ids, size_t count)
{
    const cloud_report_ops_t *ops = cloud_report_get_ops();

    if (!cloud_is_online() || (ops == NULL) || (ops->publish_properties_delta == NULL))
    {
        return;
    }
    if ((ids == NULL) || (count == 0U))
    {
        return;
    }

    sw_err_t ret = ops->publish_properties_delta(ids, count);
    if ((ret != SW_OK) && (ret != SW_ERR_COMM))
    {
        LOG_WARN("report_scheduler: publish_properties_delta failed ret=%d", (int)ret);
    }
}

static void run_policy(const report_policy_entry_t *policy, const event_t *evt)
{
    const char *id;
    const char *ids[1];

    if (policy == NULL)
    {
        return;
    }

    if (policy->use_event_param_as_point_index)
    {
        if ((evt == NULL) || (s_point_resolver == NULL))
        {
            return;
        }
        id = s_point_resolver(evt->param);
        if (id == NULL)
        {
            return;
        }
        ids[0] = id;
        report_delta_once(ids, 1U);
        return;
    }

    if (policy->full)
    {
        report_properties_once();
    }
    else
    {
        report_delta_once(policy->delta_ids, policy->delta_id_count);
    }
}

static void periodic_cb(void *ctx)
{
    size_t i;

    (void)ctx;
    link_poll_if_needed();
    cloud_point_watcher_poll();

    for (i = 0U; i < s_policy_count; i++)
    {
        if (s_policies[i].entry.kind == REPORT_TRIGGER_PERIODIC)
        {
            run_policy(&s_policies[i].entry, NULL);
        }
    }
}

void report_scheduler_request_resync(void)
{
    report_properties_once();
}

static void on_event_policy(const event_t *evt)
{
    size_t i;

    if (evt == NULL)
    {
        return;
    }

    for (i = 0U; i < s_policy_count; i++)
    {
        if ((s_policies[i].entry.kind == REPORT_TRIGGER_EVENT) &&
            (s_policies[i].entry.event_id == evt->type))
        {
            if (s_policies[i].entry.event_id == EVT_CLOUD_CONNECTED)
            {
                LOG_INFO("report_scheduler: cloud connected, resync");
            }
            run_policy(&s_policies[i].entry, evt);
        }
    }
}

sw_err_t report_scheduler_init(const report_policy_entry_t *policies, size_t count)
{
    sw_err_t ret;
    size_t   i;

    if (s_started)
    {
        return SW_OK;
    }

    if ((policies == NULL) || (count == 0U) || (count > REPORT_POLICY_MAX))
    {
        return SW_ERR_PARAM;
    }

    memset(s_policies, 0, sizeof(s_policies));
    s_policy_count = count;

    for (i = 0U; i < count; i++)
    {
        s_policies[i].entry = policies[i];
        if (policies[i].kind == REPORT_TRIGGER_PERIODIC)
        {
            if ((policies[i].period_ms == 0U) ||
                ((s_period_ms != 0U) && (s_period_ms != policies[i].period_ms)))
            {
                LOG_ERROR("report_scheduler: inconsistent periodic policy");
                return SW_ERR_PARAM;
            }
            s_period_ms = policies[i].period_ms;
        }
    }

    if (s_period_ms != 0U)
    {
        ret = periodic_task_register("cloud_report",
                                     s_period_ms,
                                     periodic_cb,
                                     NULL,
                                     SCHED_OTHER,
                                     THD_CLOUD_NICE,
                                     THD_CLOUD_STACK);
        if (ret != SW_OK)
        {
            return ret;
        }
    }

    for (i = 0U; i < count; i++)
    {
        if (policies[i].kind == REPORT_TRIGGER_EVENT)
        {
            ret = event_subscribe(policies[i].event_id, on_event_policy);
            if (ret != SW_OK)
            {
                return ret;
            }
        }
    }

    s_started = true;
    LOG_INFO("report_scheduler: init ok policies=%u", (unsigned)count);
    return SW_OK;
}
