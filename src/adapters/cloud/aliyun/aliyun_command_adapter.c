/**
 * @file    aliyun_command_adapter.c
 * @brief   阿里云 MQTT 命令下行适配器
 * @author  胡望伟
 * @date    2026-04-10
 *
 * @note    负责 MQTT 协议层（初始化连接、注册接收回调）。
 *          命令语义解析委托给 mqtt_command_parser，不含 JSON 解析逻辑。
 *          收到命令后通过 event_bus 发布 EVT_CMD_* 事件。
 */

#include "adapters/cloud/aliyun/aliyun_topics.h"
#include "adapters/ui/mqtt_cmd/mqtt_command_parser.h"
#include "ports/storage/deploy_store.h"
#include "core/event_bus/event_bus.h"
#include "common/event_types.h"
#include "middleware/snack_wrapper.h"
#include "common/log.h"
#include <string.h>
#include <stdio.h>

/* 云端凭证（从 deploy_store 读取，或使用占位符）*/
#define DEPLOY_KEY_PRODUCT_KEY    "productKey"
#define DEPLOY_KEY_DEVICE_SN      "deviceSn"
#define DEPLOY_KEY_DEVICE_SECRET  "deviceSecret"

/* -------------------------------------------------------------------------
 * MQTT 接收回调（由 snack SDK 在消息到达时调用）
 * ------------------------------------------------------------------------- */
static void mqtt_recv_cb(const char *msg)
{
    cmd_t cmd;

    if (!mqtt_command_parse(msg, &cmd))
    {
        LOG_WARN("aliyun_cmd: parse failed: %.80s", msg);
        return;
    }

    /* 将 cmd_t 转换为 event_bus 事件 */
    switch (cmd.type)
    {
        case CMD_START_WASH:
            (void)event_publish(EVT_CMD_ORDER,            (uint32_t)cmd.payload.start_wash.mode);
            break;
        case CMD_STOP_WASH:
            (void)event_publish(EVT_CMD_STOP_WASH,        0U);
            break;
        case CMD_STOP_OPERATION:
            (void)event_publish(EVT_CMD_STOP_OPERATION,   0U);
            break;
        case CMD_RESUME_OPERATION:
            (void)event_publish(EVT_CMD_RESUME_OPERATION, 0U);
            break;
        case CMD_RESET_FAULT:
            (void)event_publish(EVT_CMD_RESET_FAULT,      0U);
            break;
        case CMD_HOME_DEVICE:
            (void)event_publish(EVT_CMD_HOME_DEVICE,      0U);
            break;
        default:
            LOG_WARN("aliyun_cmd: unknown cmd type=%d", (int)cmd.type);
            break;
    }
}

/* -------------------------------------------------------------------------
 * 初始化：连接 MQTT 并注册消息接收回调
 * ------------------------------------------------------------------------- */
void aliyun_command_adapter_init(void)
{
    const deploy_store_ops_t *ds = deploy_store_get_ops();
    char product_key[64]   = "M8_PRODUCT_KEY";
    char device_sn[64]     = "M8_UNKNOWN";
    char device_secret[64] = "M8_DEVICE_SECRET";

    /* 从 deploy_store 读取凭证（失败则使用占位符，离线运行）*/
    if (ds != NULL)
    {
        (void)ds->get(DEPLOY_KEY_PRODUCT_KEY,   product_key,   sizeof(product_key));
        (void)ds->get(DEPLOY_KEY_DEVICE_SN,     device_sn,     sizeof(device_sn));
        (void)ds->get(DEPLOY_KEY_DEVICE_SECRET, device_secret, sizeof(device_secret));
    }

    if (aliyun_mqtt_init(product_key, device_sn, device_secret) == 0)
    {
        mqtt_recv_handler_set(mqtt_recv_cb);
        (void)event_publish(EVT_CLOUD_CONNECTED, 0U);
        LOG_INFO("aliyun_cmd: MQTT connected, sn=%s", device_sn);
    }
    else
    {
        LOG_WARN("aliyun_cmd: MQTT init failed, running offline");
    }
}
