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
#include <stdbool.h>
#include <string.h>

#define DEPLOY_KEY_TOPIC_UP   "topicPropertyUp"
#define REPORT_JSON_BUF_SIZE  1024U

static char s_topic_up[128] = "";
static bool s_topic_loaded  = false;

static cloud_report_build_fn_t       s_build_properties       = NULL;
static cloud_report_build_delta_fn_t s_build_properties_delta = NULL;

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

static sw_err_t send_json(const char *json)
{
    if (!mqtt_is_online())
    {
        return SW_ERR_COMM;
    }
    if (load_topic_up() != SW_OK)
    {
        return SW_ERR_PARAM;
    }
    if (net_mqtt_send(s_topic_up, (char *)json) != 0)
    {
        LOG_WARN("snack_cloud_report: send failed");
        return SW_ERR_COMM;
    }
    return SW_OK;
}

static sw_err_t adapter_publish_properties(void)
{
    char     buf[REPORT_JSON_BUF_SIZE];
    sw_err_t ret;

    if (s_build_properties == NULL)
    {
        return SW_ERR_NOT_INIT;
    }
    if (s_build_properties(buf, sizeof(buf)) != SW_OK)
    {
        LOG_ERROR("snack_cloud_report: properties build failed");
        return SW_ERR_PARAM;
    }

    ret = send_json(buf);
    return ret;
}

static sw_err_t adapter_publish_properties_delta(const char *const *ids, size_t count)
{
    char     buf[REPORT_JSON_BUF_SIZE];
    sw_err_t ret;

    if ((ids == NULL) || (count == 0U))
    {
        return SW_ERR_PARAM;
    }

    if (s_build_properties_delta != NULL)
    {
        if (s_build_properties_delta(ids, count, buf, sizeof(buf)) != SW_OK)
        {
            LOG_ERROR("snack_cloud_report: delta build failed");
            return SW_ERR_PARAM;
        }
    }
    else if (s_build_properties != NULL)
    {
        if (s_build_properties(buf, sizeof(buf)) != SW_OK)
        {
            LOG_ERROR("snack_cloud_report: fallback properties build failed");
            return SW_ERR_PARAM;
        }
    }
    else
    {
        return SW_ERR_NOT_INIT;
    }

    ret = send_json(buf);
    return ret;
}

static const cloud_report_ops_t s_ops = {
    .publish_properties       = adapter_publish_properties,
    .publish_properties_delta   = adapter_publish_properties_delta,
};

void snack_cloud_report_adapter_register(cloud_report_build_fn_t build_properties,
                                         cloud_report_build_delta_fn_t build_properties_delta)
{
    s_build_properties       = build_properties;
    s_build_properties_delta = build_properties_delta;
    cloud_report_register(&s_ops);
    LOG_INFO("snack_cloud_report: adapter registered");
}
