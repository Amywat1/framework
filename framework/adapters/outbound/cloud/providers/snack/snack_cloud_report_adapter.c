/**
 * @file    snack_cloud_report_adapter.c
 * @brief   Snack MQTT 云端状态上报出站适配器实现
 * @author  HUWANGWEI
 * @date    2026-04-10
 */

#include "framework/adapters/outbound/cloud/providers/snack/snack_cloud_report_adapter.h"
#include "framework/ports/outbound/cloud/report/report_port.h"
#include "framework/ports/outbound/cloud/link/cloud_link_port.h"
#include "framework/ports/outbound/storage/deploy_store.h"
#include "framework/cloud/cloud_model.h"
#include "framework/cloud/cloud_point.h"
#include "framework/common/log.h"
#include <stdbool.h>
#include <string.h>

#define DEPLOY_KEY_TOPIC_UP   "topicPropertyUp"

static char s_topic_up[128] = "";
static bool s_topic_loaded  = false;

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
    const cloud_link_ops_t *link = cloud_link_get_ops();

    if ((link == NULL) || (link->is_online == NULL) || !link->is_online())
    {
        return SW_ERR_COMM;
    }
    if (load_topic_up() != SW_OK)
    {
        return SW_ERR_PARAM;
    }
    if ((link->publish == NULL))
    {
        return SW_ERR_NOT_INIT;
    }
    return link->publish(s_topic_up, json);
}

static sw_err_t adapter_publish_properties(void)
{
    char     buf[CLOUD_REPORT_JSON_MAX];
    sw_err_t ret;

    if (cloud_model_build_properties(buf, sizeof(buf)) != SW_OK)
    {
        LOG_ERROR("snack_cloud_report: properties build failed");
        return SW_ERR_PARAM;
    }

    ret = send_json(buf);
    return ret;
}

static sw_err_t adapter_publish_properties_delta(const char *const *ids, size_t count)
{
    char     buf[CLOUD_REPORT_JSON_MAX];
    sw_err_t ret;

    if ((ids == NULL) || (count == 0U))
    {
        return SW_ERR_PARAM;
    }

    if (cloud_model_build_properties_delta(ids, count, buf, sizeof(buf)) != SW_OK)
    {
        LOG_ERROR("snack_cloud_report: delta build failed");
        return SW_ERR_PARAM;
    }

    ret = send_json(buf);
    return ret;
}

static const cloud_report_ops_t s_ops = {
    .publish_properties       = adapter_publish_properties,
    .publish_properties_delta = adapter_publish_properties_delta,
};

void snack_cloud_report_adapter_register(void)
{
    cloud_report_register(&s_ops);
    LOG_INFO("snack_cloud_report: adapter registered");
}
