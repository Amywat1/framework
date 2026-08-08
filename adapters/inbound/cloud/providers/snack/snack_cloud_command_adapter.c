/**
 * @file    snack_cloud_command_adapter.c
 * @brief   Snack MQTT 云端属性入站适配器实现
 * @author  HUWANGWEI
 * @date    2026-04-10
 */

#include "adapters/inbound/cloud/providers/snack/snack_cloud_command_adapter.h"

#include "common/log.h"
#include "common/point_table/point_table.h"
#include "application/ports/inbound/cloud/property/property_port.h"
#include "application/ports/outbound/cloud/link/cloud_link_port.h"

#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#define REPLY_JSON_BUF_SIZE   128U
#define SNACK_CLOUD_TOPIC_MAX 128U

static char s_topic_reply[SNACK_CLOUD_TOPIC_MAX] = "";

sw_err_t snack_cloud_property_reply(const char *request_json, const point_apply_result_t *result)
{
    const cloud_link_ops_t *link = cloud_link_get_ops();
    char                    buf[REPLY_JSON_BUF_SIZE];
    size_t                  applied  = 0U;
    size_t                  rejected = 0U;

    (void)request_json;

    if (s_topic_reply[0] == '\0') {
        return SW_OK;
    }

    if (result != NULL) {
        applied  = result->applied;
        rejected = result->rejected;
    }

    (void)snprintf(buf, sizeof(buf), "{\"applied\":%u,\"rejected\":%u}", (unsigned)applied, (unsigned)rejected);

    if ((link == NULL) || (link->publish == NULL)) {
        return SW_ERR_NOT_INIT;
    }

    if (link->publish(s_topic_reply, buf) != SW_OK) {
        LOG_WARN("snack_cloud_cmd: reply send failed");
        return SW_ERR_COMM;
    }

    return SW_OK;
}

static void mqtt_recv_cb(const char *msg)
{
    const cloud_property_ops_t *ops = cloud_property_get_ops();
    point_apply_result_t        result;
    sw_err_t                    ret;

    if (ops == NULL) {
        LOG_WARN("snack_cloud_cmd: property_port not registered");
        return;
    }
    if (ops->on_property_set == NULL) {
        LOG_WARN("snack_cloud_cmd: on_property_set not registered");
        return;
    }

    point_apply_result_init(&result);
    ret = ops->on_property_set(msg, &result);
    if (ret != SW_OK) {
        LOG_WARN("snack_cloud_cmd: property_set failed ret=%d", (int)ret);
    }

    if (ops->reply_property_set != NULL) {
        (void)ops->reply_property_set(msg, &result);
    }
}

sw_err_t snack_cloud_command_adapter_register(void)
{
    s_topic_reply[0] = '\0';
    LOG_INFO("snack_cloud_cmd: adapter registered");
    return SW_OK;
}

sw_err_t snack_cloud_command_adapter_configure(const char *topic_property_reply)
{
    if ((topic_property_reply == NULL) || (topic_property_reply[0] == '\0')) {
        s_topic_reply[0] = '\0';
        return SW_OK;
    }
    if (strlen(topic_property_reply) >= sizeof(s_topic_reply)) {
        return SW_ERR_PARAM;
    }

    (void)snprintf(s_topic_reply, sizeof(s_topic_reply), "%s", topic_property_reply);
    return SW_OK;
}

sw_err_t snack_cloud_command_adapter_bind(void)
{
    const cloud_link_ops_t *link = cloud_link_get_ops();

    if ((link == NULL) || (link->set_recv_handler == NULL)) {
        LOG_WARN("snack_cloud_cmd: cloud_link not registered");
        return SW_ERR_NOT_INIT;
    }

    link->set_recv_handler(mqtt_recv_cb);
    LOG_INFO("snack_cloud_cmd: recv handler bound");
    return SW_OK;
}
