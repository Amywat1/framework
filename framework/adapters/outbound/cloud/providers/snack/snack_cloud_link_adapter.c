/**
 * @file    snack_cloud_link_adapter.c
 * @brief   Snack MQTT 云端链路适配器（传输 + 连接边沿）
 * @author  HUWANGWEI
 * @date    2026-07-08
 */

#include "framework/adapters/outbound/cloud/providers/snack/snack_cloud_link_adapter.h"
#include "framework/ports/outbound/cloud/link/cloud_link_port.h"
#include "framework/ports/outbound/storage/deploy_store.h"
#include "framework/adapters/runtime/snack/snack_mqtt.h"
#include "framework/runtime/event_bus/event_bus.h"
#include "framework/common/event_types.h"
#include "framework/common/log.h"
#include <stdbool.h>
#include <string.h>

#define DEPLOY_KEY_PRODUCT_KEY    "productKey"
#define DEPLOY_KEY_DEVICE_SN      "deviceName"
#define DEPLOY_KEY_DEVICE_SECRET  "deviceSecret"

static cloud_link_recv_fn_t s_recv_handler   = NULL;
static bool                 s_initialized    = false;
static bool                 s_last_online    = false;
static bool                 s_started        = false;

static sw_err_t link_init(void)
{
    const deploy_store_ops_t *ds = deploy_store_get_ops();
    char product_key[64]         = "";
    char device_sn[64]           = "";
    char device_secret[64]       = "";

    if (s_initialized)
    {
        const cloud_link_ops_t *ops = cloud_link_get_ops();
        if ((ops != NULL) && (ops->is_online != NULL) && ops->is_online())
        {
            return SW_OK;
        }
        return SW_ERR_COMM;
    }

    s_initialized = true;

    if (ds != NULL)
    {
        (void)ds->get(DEPLOY_KEY_PRODUCT_KEY,   product_key,   sizeof(product_key));
        (void)ds->get(DEPLOY_KEY_DEVICE_SN,     device_sn,     sizeof(device_sn));
        (void)ds->get(DEPLOY_KEY_DEVICE_SECRET, device_secret, sizeof(device_secret));
    }

    if (aliyun_mqtt_init(product_key, device_sn, device_secret) == 0)
    {
        LOG_INFO("snack_cloud_link: connected sn=%s", device_sn);
        return SW_OK;
    }

    LOG_WARN("snack_cloud_link: init failed, running offline");
    return SW_ERR_COMM;
}

static bool link_is_online(void)
{
    return mqtt_is_online() != 0;
}

static void publish_connection_event(bool connected)
{
    if (connected)
    {
        (void)event_publish(EVT_CLOUD_CONNECTED, 0U);
        LOG_INFO("snack_cloud_link: connected");
    }
    else
    {
        (void)event_publish(EVT_CLOUD_DISCONNECTED, 0U);
        LOG_WARN("snack_cloud_link: disconnected");
    }
}

static void link_poll(void)
{
    bool now_online = link_is_online();

    if (now_online == s_last_online)
    {
        return;
    }

    s_last_online = now_online;
    publish_connection_event(now_online);
}

static sw_err_t link_publish(const char *topic, const char *payload)
{
    if (!link_is_online())
    {
        return SW_ERR_COMM;
    }
    if ((topic == NULL) || (payload == NULL))
    {
        return SW_ERR_PARAM;
    }
    if (net_mqtt_send((char *)topic, (char *)payload) != 0)
    {
        return SW_ERR_COMM;
    }
    return SW_OK;
}

static void link_set_recv_handler(cloud_link_recv_fn_t cb)
{
    s_recv_handler = cb;
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
    cloud_link_register(&s_ops);
    LOG_INFO("snack_cloud_link: adapter registered");
}

sw_err_t snack_cloud_link_adapter_init(void)
{
    const cloud_link_ops_t *ops = cloud_link_get_ops();

    if ((ops == NULL) || (ops->init == NULL))
    {
        return SW_ERR_NOT_INIT;
    }
    return ops->init();
}

sw_err_t snack_cloud_link_adapter_start(void)
{
    if (s_started)
    {
        return SW_OK;
    }
    s_started = true;

    s_last_online = link_is_online();
    if (s_last_online)
    {
        publish_connection_event(true);
    }

    LOG_INFO("snack_cloud_link: monitor started (poll via report_scheduler)");
    return SW_OK;
}
