/**
 * @file    aliyun_adapter.h
 * @brief   阿里云 MQTT 适配器接口（命令下行 + 状态上报）
 * @author  HUWANGWEI
 * @date    2026-04-10
 */

#ifndef ADAPTERS_CLOUD_ALIYUN_ADAPTER_H
#define ADAPTERS_CLOUD_ALIYUN_ADAPTER_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief  初始化阿里云 MQTT 连接并注册命令下行回调
 * @retval true   MQTT 连接成功
 * @retval false  连接失败，进入离线模式
 */
bool aliyun_command_adapter_init(void);

/**
 * @brief  注册阿里云状态上报实现到 cloud_report_port
 * @note   须在 wiring 阶段调用，先于任何上报操作
 */
void aliyun_report_adapter_register(void);

#ifdef __cplusplus
}
#endif

#endif /* ADAPTERS_CLOUD_ALIYUN_ADAPTER_H */
