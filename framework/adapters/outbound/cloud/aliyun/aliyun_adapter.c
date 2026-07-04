/**
 * @file    aliyun_adapter.c
 * @brief   阿里云 MQTT 适配器实现（命令下行 + 状态上报）
 * @author  HUWANGWEI
 * @date    2026-04-10
 */

#include "framework/adapters/outbound/cloud/aliyun/aliyun_adapter.h"
#include "framework/ports/outbound/cloud/report/report_port.h"
#include "framework/ports/outbound/storage/deploy_store.h"
#include "middleware/snack/snack_mqtt.h"
#include "framework/common/log.h"
#include <stdio.h>
#include <string.h>

/* deploy_store 键名 */
#define DEPLOY_KEY_PRODUCT_KEY    "productKey"
#define DEPLOY_KEY_DEVICE_SN      "deviceName"
#define DEPLOY_KEY_DEVICE_SECRET  "deviceSecret"
#define DEPLOY_KEY_TOPIC_UP       "topicPropertyUp"

/* 上报 Topic（启动时从 deploy_store 加载） */
static char s_topic_up[128] = "";

/* 命令分发器（由 aliyun_command_adapter_init 注入） */
static aliyun_cmd_dispatch_fn_t s_cmd_dispatch = NULL;

/* 上报 JSON 构建器（由 aliyun_report_adapter_register 注入） */
static aliyun_report_builder_fn_t s_report_builder = NULL;

/* -------------------------------------------------------------------------
 * 命令下行
 * ------------------------------------------------------------------------- */
static void mqtt_recv_cb(const char *msg)
{
    if (s_cmd_dispatch == NULL)
    {
        LOG_WARN("aliyun: dispatch not registered");
        return;
    }

    s_cmd_dispatch(msg);
}

bool aliyun_command_adapter_init(aliyun_cmd_dispatch_fn_t dispatch)
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

        if ((ds->get(DEPLOY_KEY_TOPIC_UP, s_topic_up, sizeof(s_topic_up)) != SW_OK) ||
            (s_topic_up[0] == '\0'))
        {
            LOG_ERROR("aliyun: deploy config missing key=%s", DEPLOY_KEY_TOPIC_UP);
            return false;
        }
    }

    if (aliyun_mqtt_init(product_key, device_sn, device_secret) == 0)
    {
        mqtt_recv_handler_set(mqtt_recv_cb);
        LOG_INFO("aliyun: MQTT connected, sn=%s", device_sn);
        return true;
    }

    LOG_WARN("aliyun: MQTT init failed, running offline");
    return false;
}

/* -------------------------------------------------------------------------
 * 状态上报
 * ------------------------------------------------------------------------- */
static sw_err_t adapter_report(const cloud_report_payload_t *payload)
{
    char buf[256];

    if (!mqtt_is_online())
    {
        return SW_ERR_COMM;
    }
    if ((s_report_builder == NULL) ||
        (s_report_builder(payload, buf, sizeof(buf)) != SW_OK))
    {
        LOG_ERROR("aliyun: json build failed");
        return SW_ERR_PARAM;
    }
    if (net_mqtt_send(s_topic_up, buf) != 0)
    {
        LOG_WARN("aliyun: send failed");
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

void aliyun_report_adapter_register(aliyun_report_builder_fn_t builder)
{
    s_report_builder = builder;
    cloud_report_register(&s_ops);
    LOG_INFO("aliyun: report adapter registered");
}
