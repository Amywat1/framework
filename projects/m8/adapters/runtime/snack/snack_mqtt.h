/**
 * @file    snack_mqtt.h
 * @brief   snack SDK 阿里云 MQTT 接口
 */

#ifndef SNACK_MQTT_H
#define SNACK_MQTT_H

#ifdef __cplusplus
extern "C" {
#endif

/** MQTT 下行消息回调类型 */
typedef void (*mqtt_recv_handler_t)(const char *msg);

/**
 * @brief  初始化并连接阿里云 MQTT
 * @param  product_key    产品 Key
 * @param  device_name    设备名（SN）
 * @param  device_secret  设备密钥
 * @return 0 成功，非 0 失败
 */
extern int aliyun_mqtt_init(char *product_key, char *device_name, char *device_secret);

/**
 * @brief  查询 MQTT 在线状态
 * @return 1 在线，0 离线
 */
extern int mqtt_is_online(void);

/**
 * @brief  向指定 Topic 发布消息
 * @param  topic 目标 Topic
 * @param  msg   消息内容（JSON 字符串）
 * @return 0 成功，非 0 失败
 */
extern int net_mqtt_send(char *topic, char *msg);

/**
 * @brief 注册 MQTT 下行消息回调
 * @param cb 回调函数指针
 */
extern void mqtt_recv_handler_set(mqtt_recv_handler_t cb);

#ifdef __cplusplus
}
#endif

#endif /* SNACK_MQTT_H */
