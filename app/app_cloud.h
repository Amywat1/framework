/**
 * @file    app_cloud.h
 * @brief   云端通信接口（阿里云 MQTT 初始化、状态上报、消息接收）
 * @author  胡望伟
 * @date    2026-04-08
 */

#ifndef APP_CLOUD_H
#define APP_CLOUD_H

#ifdef __cplusplus
extern "C" {
#endif

#include "app/app_fsm.h"
#include "app/app_wash_steps.h"
#include "common/sw_error.h"

/**
 * @brief  初始化云端连接（阿里云 MQTT）
 * @retval SW_OK / SW_ERR_COMM
 */
sw_err_t app_cloud_init(void);

/**
 * @brief  上报设备状态到云端（有变化且在线时才推送）
 * @param  state      当前 FSM 状态
 * @param  step       当前洗车步骤
 * @param  has_alarm  是否有 ERROR 级报警
 */
void app_cloud_report(DevState_t state, WashStep_t step, bool has_alarm);

/**
 * @brief  云端下行消息回调（注册到 mqtt_recv_handler_set）
 * @param  msg  JSON 字符串（snack_wrapper 已提取 params 对象）
 */
void app_mqtt_recv_handler(const char *msg);

#ifdef __cplusplus
}
#endif

#endif /* APP_CLOUD_H */
