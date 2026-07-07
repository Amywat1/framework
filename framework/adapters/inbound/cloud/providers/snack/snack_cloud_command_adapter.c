/**
 * @file    snack_cloud_command_adapter.c
 * @brief   Snack MQTT 云端命令入站适配器实现
 * @author  HUWANGWEI
 * @date    2026-04-10
 */

#include "framework/adapters/inbound/cloud/providers/snack/snack_cloud_command_adapter.h"
#include "framework/ports/outbound/storage/deploy_store.h"
#include "framework/adapters/runtime/snack/snack_mqtt.h"
#include "framework/common/log.h"

#define DEPLOY_KEY_PRODUCT_KEY    "productKey"
#define DEPLOY_KEY_DEVICE_SN      "deviceName"
#define DEPLOY_KEY_DEVICE_SECRET  "deviceSecret"

static snack_cloud_cmd_dispatch_fn_t s_cmd_dispatch = NULL;

static void mqtt_recv_cb(const char *msg)
{
    if (s_cmd_dispatch == NULL)
    {
        LOG_WARN("snack_cloud_cmd: dispatch not registered");
        return;
    }

    s_cmd_dispatch(msg);
}

bool snack_cloud_command_adapter_init(snack_cloud_cmd_dispatch_fn_t dispatch)
{
    const deploy_store_ops_t *ds = deploy_store_get_ops();
    char product_key[64]   = "";
    char device_sn[64]     = "";
    char device_secret[64] = "";

    s_cmd_dispatch = dispatch;

    if (ds != NULL)
    {
        (void)ds->get(DEPLOY_KEY_PRODUCT_KEY,   product_key,   sizeof(product_key));
        (void)ds->get(DEPLOY_KEY_DEVICE_SN,     device_sn,     sizeof(device_sn));
        (void)ds->get(DEPLOY_KEY_DEVICE_SECRET, device_secret, sizeof(device_secret));
    }

    if (aliyun_mqtt_init(product_key, device_sn, device_secret) == 0)
    {
        mqtt_recv_handler_set(mqtt_recv_cb);
        LOG_INFO("snack_cloud_cmd: MQTT connected, sn=%s", device_sn);
        return true;
    }

    LOG_WARN("snack_cloud_cmd: MQTT init failed, running offline");
    return false;
}
