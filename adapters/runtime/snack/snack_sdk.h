/**
 * @file    snack_sdk.h
 * @brief   Snack SDK 薄 C 包装接口聚合（日志 / MQTT / 播放 / BLE / CLI / 运行时）
 *
 * @note    供真机构建路径下需要调用 Snack SDK 的文件使用；
 *          仿真构建路径不链接 Snack SDK，不得包含本头文件。
 */

#ifndef FRAMEWORK_ADAPTERS_RUNTIME_SNACK_SDK_H
#define FRAMEWORK_ADAPTERS_RUNTIME_SNACK_SDK_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

/* ---- 日志 ---- */

/**
 * @brief 将 common/log.h 的日志 sink 注册为 snack SDK 实现
 * @param name 日志实例名；传 NULL 或空串时使用默认名
 */
extern void snack_log_sink_register(const char *name);

/* ---- 运行时 ---- */

/** @brief 设置 Snack 运行时日志级别 */
void snack_runtime_set_log_level(int type);

/** @brief 设置 Snack 运行时远程调试端口 */
void snack_runtime_set_remote_port(int port);

/* ---- MQTT ---- */

/** MQTT 下行消息回调类型 */
typedef void (*mqtt_recv_handler_t)(const char *msg);

/**
 * @brief  初始化并连接阿里云 MQTT
 * @return 0 成功，非 0 失败
 */
extern int aliyun_mqtt_init(char *product_key, char *device_name, char *device_secret);

/** @brief  查询 MQTT 在线状态（1 在线，0 离线） */
extern int mqtt_is_online(void);

/**
 * @brief  向指定 Topic 发布消息
 * @return 0 成功，非 0 失败
 */
extern int net_mqtt_send(char *topic, char *msg);

/** @brief 注册 MQTT 下行消息回调 */
extern void mqtt_recv_handler_set(mqtt_recv_handler_t cb);

/* ---- 音乐播放 ---- */

/**
 * @brief 播放指定编号的音频文件
 * @param item 音频编号（对应 /home/neardi/Music/00N.mp3）
 */
extern void player_play(uint8_t item);

/* ---- BLE ---- */

/** @brief 初始化 BLE 并注册数据/调试通道回调 */
extern void ble_init(void);

/** @brief 反初始化 BLE */
extern void ble_deinit(void);

/** @brief 通过 BLE 应用数据通道发送数据 */
extern void ble_application_channel_send(char *data);

/** @brief 通过 BLE 调试通道发送数据 */
extern void ble_debug_channel_send(char *data);

/** @brief 注册 OSAL 调试命令回调（供 BLE 调试通道路由使用） */
extern void osal_debug_callback_regist(int (*callback)(char *fun, char *param_1, char *param_2));

/* ---- CLI ---- */

/** @brief 初始化 CLI SDK */
void cli_adapter_init(void);

/**
 * @brief 注册一个命令处理函数
 * @param cmd_fn 返回 int、无参数的命令函数指针
 */
void cli_adapter_add(int (*cmd_fn)(void));

/**
 * @brief 读取当前命令的第 idx 个参数
 * @param idx 参数下标，从 0 开始；超出范围时返回 NULL
 */
char *cli_adapter_get(int idx);

#ifdef __cplusplus
}
#endif

#endif /* FRAMEWORK_ADAPTERS_RUNTIME_SNACK_SDK_H */
