/**
 * @file    app_cloud.c
 * @brief   云端通信实现（阿里云 MQTT 初始化、状态上报、消息接收）
 * @author  胡望伟
 * @date    2026-04-08
 */

#include "app_cloud.h"
#include "app_fsm.h"
#include "service/svc_param.h"
#include "common/log.h"
#include "tools/cJSON.h"
#include "middleware/snack_wrapper.h"
#include <string.h>
#include <stdio.h>

/* -------------------------------------------------------------------------
 * 云端初始化
 * ------------------------------------------------------------------------- */
sw_err_t app_cloud_init(void)
{
    char sn[64] = {0};
    (void)svc_param_get_str(PARAM_KEY_DEVICE_SN, sn, sizeof(sn), "M8_UNKNOWN");

    /* TODO: 替换为实际 product_key、device_secret */
    if (aliyun_mqtt_init((char *)"M8_PRODUCT_KEY", sn,
                         (char *)"M8_DEVICE_SECRET") == 0) {
        mqtt_recv_handler_set(app_mqtt_recv_handler);
        LOG_INFO("app_cloud_init: MQTT connected, sn=%s", sn);
    } else {
        LOG_WARN("app_cloud_init: MQTT init failed, running offline");
    }

    return SW_OK;
}

/* -------------------------------------------------------------------------
 * 设备状态上报（有变化且在线时才推送）
 * ------------------------------------------------------------------------- */
void app_cloud_report(DevState_t state, WashStep_t step, bool has_alarm)
{
    static DevState_t last_state = (DevState_t)-1;
    static WashStep_t last_step  = (WashStep_t)-1;
    static bool       last_alarm = false;

    if ((state == last_state) && (step == last_step) && (has_alarm == last_alarm)) {
        return;
    }

    last_state = state;
    last_step  = step;
    last_alarm = has_alarm;

    if (!mqtt_is_online()) {
        return;
    }

    char buf[256];
    snprintf(buf, sizeof(buf),
             "{\"devState\":%d,\"washStep\":%d,\"hasAlarm\":%d}",
             (int)state, (int)step, (int)has_alarm);

    /* TODO: 替换为实际云端上报 topic */
    (void)net_mqtt_send((char *)"/m8/property/up", buf);
}

/* -------------------------------------------------------------------------
 * 云端下行消息处理
 * ------------------------------------------------------------------------- */
void app_mqtt_recv_handler(const char *msg)
{
    cJSON *root = cJSON_Parse(msg);
    if (root == NULL) {
        LOG_WARN("app_mqtt_recv: invalid JSON");
        return;
    }

    cJSON *action = cJSON_GetObjectItem(root, "action");
    if (cJSON_IsString(action) && (action->valuestring != NULL)) {
        if (strcmp(action->valuestring, "order") == 0) {
            app_fsm_post_cmd(DEV_CMD_ORDER);
        } else if (strcmp(action->valuestring, "stop") == 0) {
            app_fsm_post_cmd(DEV_CMD_STOP);
        } else if (strcmp(action->valuestring, "resume") == 0) {
            app_fsm_post_cmd(DEV_CMD_RESUME);
        } else if (strcmp(action->valuestring, "reset") == 0) {
            app_fsm_post_cmd(DEV_CMD_RESET);
        }
    }

    cJSON_Delete(root);
}
