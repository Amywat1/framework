/**
 * @file    snack_cloud_report_adapter.c
 * @brief   Snack MQTT 云端状态上报出站适配器实现
 * @author  HUWANGWEI
 * @date    2026-04-10
 */

#include "adapters/outbound/cloud/providers/snack/snack_cloud_report_adapter.h"

#include "adapters/outbound/cloud/cloud_model_json.h"
#include "application/ports/outbound/cloud/link/cloud_link_port.h"
#include "application/ports/outbound/cloud/report/report_port.h"
#include "common/log.h"
#include "domain/cloud/cloud_model.h"
#include "domain/cloud/cloud_point.h"

#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#define SNACK_CLOUD_TOPIC_MAX 128U

static char s_topic_up[SNACK_CLOUD_TOPIC_MAX] = "";

static sw_err_t send_json(const char *json)
{
    const cloud_link_ops_t *link = cloud_link_get_ops();

    if ((link == NULL) || (link->is_online == NULL) || !link->is_online()) {
        return SW_ERR_COMM;
    }
    if (s_topic_up[0] == '\0') {
        return SW_ERR_PARAM;
    }
    if ((link->publish == NULL)) {
        return SW_ERR_NOT_INIT;
    }
    return link->publish(s_topic_up, json);
}

static sw_err_t adapter_publish_properties(void)
{
    char     buf[CLOUD_REPORT_JSON_MAX];
    sw_err_t ret;

    if (cloud_model_build_properties(buf, sizeof(buf)) != SW_OK) {
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

    if ((ids == NULL) || (count == 0U)) {
        return SW_ERR_PARAM;
    }

    if (cloud_model_build_properties_delta(ids, count, buf, sizeof(buf)) != SW_OK) {
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
    s_topic_up[0] = '\0';
    cloud_report_register(&s_ops);
    LOG_INFO("snack_cloud_report: adapter registered");
}

sw_err_t snack_cloud_report_adapter_configure(const char *topic_property_up)
{
    if ((topic_property_up == NULL) || (topic_property_up[0] == '\0')) {
        return SW_ERR_PARAM;
    }
    if (strlen(topic_property_up) >= sizeof(s_topic_up)) {
        return SW_ERR_PARAM;
    }

    (void)snprintf(s_topic_up, sizeof(s_topic_up), "%s", topic_property_up);
    return SW_OK;
}
