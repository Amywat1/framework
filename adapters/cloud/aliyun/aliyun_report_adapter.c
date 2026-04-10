/**
 * @file    aliyun_report_adapter.c
 * @brief   阿里云 MQTT 状态上报适配器（实现 cloud_report_ops_t）
 * @author  胡望伟
 * @date    2026-04-10
 */

#include "adapters/cloud/aliyun/aliyun_topics.h"
#include "ports/cloud/report_port.h"
#include "middleware/snack_wrapper.h"
#include "common/log.h"
#include <stdio.h>
#include <string.h>

/* -------------------------------------------------------------------------
 * 内部：构建上报 JSON（固定格式，不使用 cJSON 避免动态分配）
 * ------------------------------------------------------------------------- */
static sw_err_t build_json(const cloud_report_payload_t *p,
                            char *buf, size_t buf_size)
{
    int n = snprintf(buf, buf_size,
                     "{\"%s\":%d,\"%s\":%d,\"%s\":%d,"
                     "\"%s\":%d,\"%s\":%s,\"%s\":%u,\"%s\":%s}",
                     REPORT_FIELD_DEV_STATE,   (int)p->dev_state,
                     REPORT_FIELD_WASH_MODE,   (int)p->wash_mode,
                     REPORT_FIELD_WASH_STEP,   (int)p->wash_step,
                     REPORT_FIELD_GANTRY_POS,  (int)p->gantry_pos,
                     REPORT_FIELD_HAS_ALARM,   p->has_alarm ? "true" : "false",
                     REPORT_FIELD_ALARM_CODE,  (unsigned)p->alarm_code,
                     REPORT_FIELD_CLOUD_CONN,  p->cloud_connected ? "true" : "false");

    return ((n > 0) && ((size_t)n < buf_size)) ? SW_OK : SW_ERR_PARAM;
}

/* -------------------------------------------------------------------------
 * ops 实现
 * ------------------------------------------------------------------------- */
static sw_err_t adapter_report(const cloud_report_payload_t *payload)
{
    char buf[256];

    if (!mqtt_is_online())
    {
        return SW_ERR_COMM;
    }
    if (build_json(payload, buf, sizeof(buf)) != SW_OK)
    {
        LOG_ERROR("aliyun_report: json build failed");
        return SW_ERR_PARAM;
    }
    if (net_mqtt_send((char *)M8_TOPIC_PROPERTY_UP, buf) != 0)
    {
        LOG_WARN("aliyun_report: send failed");
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
    LOG_INFO("aliyun_report_adapter: registered");
}
