/**
 * @file    snack_ble.h
 * @brief   snack SDK BLE 接口
 */

#ifndef SNACK_BLE_H
#define SNACK_BLE_H

#ifdef __cplusplus
extern "C" {
#endif

/** @brief 初始化 BLE 并注册数据/调试通道回调 */
extern void ble_init(void);

/** @brief 反初始化 BLE */
extern void ble_deinit(void);

/**
 * @brief 通过 BLE 应用数据通道发送数据
 * @param data 待发送字符串
 */
extern void ble_application_channel_send(char *data);

/**
 * @brief 通过 BLE 调试通道发送数据
 * @param data 待发送字符串
 */
extern void ble_debug_channel_send(char *data);

/**
 * @brief 注册 OSAL 调试命令回调（供 BLE 调试通道路由使用）
 * @param callback 回调函数，参数依次为命令名、参数1、参数2
 */
extern void osal_debug_callback_regist(int (*callback)(char *fun, char *param_1, char *param_2));

#ifdef __cplusplus
}
#endif

#endif /* SNACK_BLE_H */
