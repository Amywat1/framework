/**
 * @file    snack_cloud_link_adapter.c
 * @brief   Snack MQTT 云端链路适配器（传输 + 连接边沿）
 * @author  HUWANGWEI
 * @date    2026-07-08
 */

#include "adapters/outbound/cloud/providers/snack/snack_cloud_link_adapter.h"

#include "adapters/runtime/snack/snack_sdk.h"
#include "common/event_types.h"
#include "common/log.h"
#include "ports/outbound/cloud/link/cloud_link_port.h"
#include "runtime/event_bus/event_bus.h"

#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#define SNACK_CLOUD_CRED_MAX 64U

static bool s_initialized                         = false;
static bool s_bootstrapped                        = false;
static bool s_last_online                         = false;
static char s_product_key[SNACK_CLOUD_CRED_MAX]   = "";
static char s_device_sn[SNACK_CLOUD_CRED_MAX]     = "";
static char s_device_secret[SNACK_CLOUD_CRED_MAX] = "";

static bool link_is_online(void)
{
    return mqtt_is_online() != 0;
}

static void publish_connection_event(bool connected)
{
    if (connected) {
        (void)event_publish(EVT_CLOUD_CONNECTED, 0U);
        LOG_INFO("snack_cloud_link: connected");
    } else {
        (void)event_publish(EVT_CLOUD_DISCONNECTED, 0U);
        LOG_WARN("snack_cloud_link: disconnected");
    }
}

static void link_bootstrap_once(void)
{
    if (s_bootstrapped) {
        return;
    }
    s_bootstrapped = true;

    s_last_online = link_is_online();
    if (s_last_online) {
        publish_connection_event(true);
    }

    LOG_INFO("snack_cloud_link: bootstrap ok");
}

static sw_err_t link_init(void)
{
    sw_err_t ret = SW_ERR_COMM;

    if ((s_product_key[0] == '\0') || (s_device_sn[0] == '\0') || (s_device_secret[0] == '\0')) {
        return SW_ERR_NOT_INIT;
    }

    if (s_initialized) {
        link_bootstrap_once();
        return link_is_online() ? SW_OK : SW_ERR_COMM;
    }

    s_initialized = true;

    if (aliyun_mqtt_init(s_product_key, s_device_sn, s_device_secret) == 0) {
        LOG_INFO("snack_cloud_link: connected sn=%s", s_device_sn);
        ret = SW_OK;
    } else {
        LOG_WARN("snack_cloud_link: init failed, running offline");
    }

    link_bootstrap_once();
    return ret;
}

static void link_poll(void)
{
    bool now_online = link_is_online();

    if (now_online == s_last_online) {
        return;
    }

    s_last_online = now_online;
    publish_connection_event(now_online);
}

static sw_err_t link_publish(const char *topic, const char *payload)
{
    if (!link_is_online()) {
        return SW_ERR_COMM;
    }
    if ((topic == NULL) || (payload == NULL)) {
        return SW_ERR_PARAM;
    }
    if (net_mqtt_send((char *)topic, (char *)payload) != 0) {
        return SW_ERR_COMM;
    }
    return SW_OK;
}

static void link_set_recv_handler(cloud_link_recv_fn_t cb)
{
    mqtt_recv_handler_set((mqtt_recv_handler_t)cb);
}

static const cloud_link_ops_t s_ops = {
    .init             = link_init,
    .is_online        = link_is_online,
    .poll             = link_poll,
    .publish          = link_publish,
    .set_recv_handler = link_set_recv_handler,
};

void snack_cloud_link_adapter_register(void)
{
    s_initialized      = false;
    s_bootstrapped     = false;
    s_last_online      = false;
    s_product_key[0]   = '\0';
    s_device_sn[0]     = '\0';
    s_device_secret[0] = '\0';
    cloud_link_register(&s_ops);
    LOG_INFO("snack_cloud_link: adapter registered");
}

sw_err_t snack_cloud_link_adapter_configure(const char *product_key, const char *device_sn, const char *device_secret)
{
    if ((product_key == NULL) || (device_sn == NULL) || (device_secret == NULL) || (product_key[0] == '\0')
        || (device_sn[0] == '\0') || (device_secret[0] == '\0')) {
        return SW_ERR_PARAM;
    }
    if ((strlen(product_key) >= sizeof(s_product_key)) || (strlen(device_sn) >= sizeof(s_device_sn))
        || (strlen(device_secret) >= sizeof(s_device_secret))) {
        return SW_ERR_PARAM;
    }

    (void)snprintf(s_product_key, sizeof(s_product_key), "%s", product_key);
    (void)snprintf(s_device_sn, sizeof(s_device_sn), "%s", device_sn);
    (void)snprintf(s_device_secret, sizeof(s_device_secret), "%s", device_secret);
    return SW_OK;
}
