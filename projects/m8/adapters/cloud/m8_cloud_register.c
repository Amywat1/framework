/**
 * @file    m8_cloud_register.c
 * @brief   M8 云端物模型一次注册实现
 * @author  HUWANGWEI
 * @date    2026-07-08
 */

#include "projects/m8/adapters/cloud/m8_cloud_register.h"
#include "projects/m8/adapters/cloud/m8_cloud_model.h"
#include "framework/cloud/cloud_model.h"
#include "framework/adapters/inbound/cloud/providers/snack/snack_cloud_command_adapter.h"
#include "framework/application/orchestrators/report_scheduler.h"
#include "framework/runtime/config/thread_config.h"
#include "framework/common/event_types.h"

static const char *const s_alarm_report_ids[] = {
    "sts_dev_warning",
    "sts_emergency",
};

static const report_policy_entry_t s_m8_report_policies[] = {
    {
        .kind                           = REPORT_TRIGGER_PERIODIC,
        .period_ms                      = THD_CLOUD_REPORT_PERIOD_MS,
        .event_id                       = 0U,
        .full                           = true,
        .use_event_param_as_point_index = false,
        .delta_ids                      = NULL,
        .delta_id_count                 = 0U,
    },
    {
        .kind                           = REPORT_TRIGGER_EVENT,
        .period_ms                      = 0U,
        .event_id                       = EVT_CLOUD_CONNECTED,
        .full                           = true,
        .use_event_param_as_point_index = false,
        .delta_ids                      = NULL,
        .delta_id_count                 = 0U,
    },
    {
        .kind                           = REPORT_TRIGGER_EVENT,
        .period_ms                      = 0U,
        .event_id                       = EVT_ALARM_TRIGGERED,
        .full                           = false,
        .use_event_param_as_point_index = false,
        .delta_ids                      = s_alarm_report_ids,
        .delta_id_count                 = sizeof(s_alarm_report_ids) / sizeof(s_alarm_report_ids[0]),
    },
    {
        .kind                           = REPORT_TRIGGER_EVENT,
        .period_ms                      = 0U,
        .event_id                       = EVT_ALARM_CLEARED,
        .full                           = false,
        .use_event_param_as_point_index = false,
        .delta_ids                      = s_alarm_report_ids,
        .delta_id_count                 = sizeof(s_alarm_report_ids) / sizeof(s_alarm_report_ids[0]),
    },
    {
        .kind                           = REPORT_TRIGGER_EVENT,
        .period_ms                      = 0U,
        .event_id                       = EVT_CLOUD_POINT_DIRTY,
        .full                           = false,
        .use_event_param_as_point_index = true,
        .delta_ids                      = NULL,
        .delta_id_count                 = 0U,
    },
};

sw_err_t m8_cloud_register(void)
{
    size_t                     count;
    const cloud_point_entry_t *entries = m8_cloud_model(&count);
    cloud_model_bundle_t       bundle;

    bundle.entries         = entries;
    bundle.count           = count;
    bundle.report_policies = s_m8_report_policies;
    bundle.policy_count    = sizeof(s_m8_report_policies) / sizeof(s_m8_report_policies[0]);
    bundle.property_reply  = snack_cloud_property_reply;

    return cloud_model_register(&bundle);
}
