/**
 * @file    snack_cloud_report_adapter.c
 * @brief   Snack MQTT 云端状态上报出站适配器实现
 * @author  HUWANGWEI
 * @date    2026-04-10
 */

#include "framework/adapters/outbound/cloud/providers/snack/snack_cloud_report_adapter.h"
#include "framework/ports/outbound/cloud/report/report_port.h"
#include "framework/ports/outbound/storage/deploy_store.h"
#include "framework/adapters/runtime/snack/snack_mqtt.h"
#include "framework/common/log.h"
#include <string.h>

#define DEPLOY_KEY_TOPIC_UP   "topicPropertyUp"

static char s_topic_up[128] = "";
static bool s_topic_loaded  = false;

static snack_cloud_report_builder_fn_t s_report_builder = NULL;

static sw_err_t load_topic_up(void)
{
    const deploy_store_ops_t *ds = deploy_store_get_ops();

    if (s_topic_loaded)
    {
        return (s_topic_up[0] != '\0') ? SW_OK : SW_ERR_PARAM;
    }

    s_topic_loaded = true;
    if (ds == NULL)
    {
        return SW_ERR_NOT_INIT;
    }

    if ((ds->get(DEPLOY_KEY_TOPIC_UP, s_topic_up, sizeof(s_topic_up)) != SW_OK) ||
        (s_topic_up[0] == '\0'))
    {
        LOG_ERROR("snack_cloud_report: deploy config missing key=%s", DEPLOY_KEY_TOPIC_UP);
        return SW_ERR_PARAM;
    }

    return SW_OK;
}

static sw_err_t adapter_report(const cloud_report_payload_t *payload)
{
    char buf[256];

    if (!mqtt_is_online())
    {
        return SW_ERR_COMM;
    }
    if (load_topic_up() != SW_OK)
    {
        return SW_ERR_PARAM;
    }
    if ((s_report_builder == NULL) ||
        (s_report_builder(payload, buf, sizeof(buf)) != SW_OK))
    {
        LOG_ERROR("snack_cloud_report: json build failed");
        return SW_ERR_PARAM;
    }
    if (net_mqtt_send(s_topic_up, buf) != 0)
    {
        LOG_WARN("snack_cloud_report: send failed");
        return SW_ERR_COMM;
    }
    return SW_OK;
}

static bool adapter_is_connected(void)
{
    return mqtt_is_online() != 0;
}

static const cloud_report_ops_t s_ops = {
    .report       = adapter_report,
    .is_connected = adapter_is_connected,
};

void snack_cloud_report_adapter_register(snack_cloud_report_builder_fn_t builder)
{
    s_report_builder = builder;
    cloud_report_register(&s_ops);
    LOG_INFO("snack_cloud_report: adapter registered");
}
