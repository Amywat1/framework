/**
 * @file    snack_wrapper.h
 * @brief   snack SDK C 语言封装接口（日志、MQTT 阿里云、音乐、BLE）
 * @author  胡望伟
 * @date    2026-04-08
 */

#ifndef SNACK_WRAPPER_H
#define SNACK_WRAPPER_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <stdint.h>

/* -------------------------------------------------------------------------
 * 阿里云 MQTT
 * ------------------------------------------------------------------------- */
typedef void (*mqtt_recv_handler_t)(const char *msg);

extern int  aliyun_mqtt_init(char *product_key, char *device_name, char *device_secret);
extern int  mqtt_is_online(void);
extern int  net_mqtt_send(char *topic, char *msg);
extern void mqtt_recv_handler_set(mqtt_recv_handler_t cb);

/* -------------------------------------------------------------------------
 * 日志
 * ------------------------------------------------------------------------- */
extern void snack_log_error(const char *fmt, ...);
extern void snack_log_warn(const char *fmt, ...);
extern void snack_log_info(const char *fmt, ...);
extern void snack_log_debug(const char *fmt, ...);
extern void set_log_level(int type);

/* -------------------------------------------------------------------------
 * 音乐播放
 * ------------------------------------------------------------------------- */
extern void player_play(uint8_t item);

/* -------------------------------------------------------------------------
 * BLE
 * ------------------------------------------------------------------------- */
extern void ble_init(void);
extern void ble_deinit(void);
extern void ble_application_channel_send(char *data);
extern void ble_debug_channel_send(char *data);

/* -------------------------------------------------------------------------
 * osal 调试回调（供 BLE 调试通道路由使用）
 * ------------------------------------------------------------------------- */
extern void osal_debug_callback_regist(int (*callback)(char *fun, char *param_1, char *param_2));

#ifdef __cplusplus
}
#endif

#endif /* SNACK_WRAPPER_H */
