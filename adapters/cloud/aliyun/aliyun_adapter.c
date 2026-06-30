/**
 * @file    aliyun_adapter.c
 * @brief   阿里云 MQTT 适配器实现（命令下行 + 状态上报）
 * @author  HUWANGWEI
 * @date    2026-04-10
 */

#include "adapters/cloud/aliyun/aliyun_adapter.h"
#include "adapters/ui/mqtt_cmd/mqtt_command_parser.h"
#include "ports/cloud/command_port.h"
#include "ports/cloud/report_port.h"
#include "ports/storage/deploy_store.h"
#include "middleware/snack/snack_mqtt.h"
#include "common/log.h"
#include <stdio.h>
#include <string.h>

/* -------------------------------------------------------------------------
 * 上报 JSON 字段名
 * ------------------------------------------------------------------------- */
#define FIELD_DEV_STATE     "devState"
#define FIELD_WASH_MODE     "washMode"
#define FIELD_GANTRY_POS    "gantryPos"
#define FIELD_HAS_ALARM     "hasAlarm"
#define FIELD_ALARM_CODE    "alarmCode"
#define FIELD_CLOUD_CONN    "cloudConn"

/* deploy_store 键名 */
#define DEPLOY_KEY_PRODUCT_KEY    "productKey"
#define DEPLOY_KEY_DEVICE_SN      "deviceSn"
#define DEPLOY_KEY_DEVICE_SECRET  "deviceSecret"
#define DEPLOY_KEY_TOPIC_UP       "topicPropertyUp"

/* 上报 Topic（启动时从 deploy_store 加载） */
static char s_topic_up[128] = "";

/* -------------------------------------------------------------------------
 * 命令下行
 * ------------------------------------------------------------------------- */
static void mqtt_recv_cb(const char *msg)
{
    cmd_t                      cmd;
    const command_port_ops_t  *cp = command_port_get_ops();

    if (!mqtt_command_parse(msg, &cmd))
    {
        LOG_WARN("aliyun: parse failed: %.80s", msg);
        return;
    }

    if (cp == NULL)
    {
        LOG_WARN("aliyun: command_port not registered");
        return;
    }

    (void)cp->inject(&cmd);
}

bool aliyun_command_adapter_init(void)
{
    const deploy_store_ops_t *ds = deploy_store_get_ops();
    char product_key[64]   = "";
    char device_sn[64]     = "";
    char device_secret[64] = "";

    if (ds != NULL)
    {
        (void)ds->get(DEPLOY_KEY_PRODUCT_KEY,   product_key,   sizeof(product_key));
        (void)ds->get(DEPLOY_KEY_DEVICE_SN,     device_sn,     sizeof(device_sn));
        (void)ds->get(DEPLOY_KEY_DEVICE_SECRET, device_secret, sizeof(device_secret));
        (void)ds->get(DEPLOY_KEY_TOPIC_UP,      s_topic_up,    sizeof(s_topic_up));
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
static sw_err_t build_json(const cloud_report_payload_t *p,
                            char *buf, size_t buf_size)
{
    int n = snprintf(buf, buf_size,
                     "{\"%s\":%d,\"%s\":%d,\"%s\":%d,"
                     "\"%s\":%s,\"%s\":%u,\"%s\":%s}",
                     FIELD_DEV_STATE,   (int)p->dev_state,
                     FIELD_WASH_MODE,   (int)p->wash_mode,
                     FIELD_GANTRY_POS,  (int)p->gantry_pos,
                     FIELD_HAS_ALARM,   p->has_alarm ? "true" : "false",
                     FIELD_ALARM_CODE,  (unsigned)p->alarm_code,
                     FIELD_CLOUD_CONN,  p->cloud_connected ? "true" : "false");

    return ((n > 0) && ((size_t)n < buf_size)) ? SW_OK : SW_ERR_PARAM;
}

static sw_err_t adapter_report(const cloud_report_payload_t *payload)
{
    char buf[256];

    if (!mqtt_is_online())
    {
        return SW_ERR_COMM;
    }
    if (build_json(payload, buf, sizeof(buf)) != SW_OK)
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

void aliyun_report_adapter_register(void)
{
    cloud_report_register(&s_ops);
    LOG_INFO("aliyun: report adapter registered");
}
