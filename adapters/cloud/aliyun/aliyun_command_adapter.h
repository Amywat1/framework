/**
 * @file    aliyun_command_adapter.h
 * @brief   阿里云 MQTT 命令下行适配器接口
 * @author  HUWANGWEI
 * @date    2026-06-23
 */

#ifndef ADAPTERS_CLOUD_ALIYUN_COMMAND_ADAPTER_H
#define ADAPTERS_CLOUD_ALIYUN_COMMAND_ADAPTER_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief  初始化阿里云 MQTT 命令适配器
 * @retval true   MQTT 连接成功
 * @retval false  MQTT 初始化失败，进入离线模式
 */
bool aliyun_command_adapter_init(void);

#ifdef __cplusplus
}
#endif

#endif /* ADAPTERS_CLOUD_ALIYUN_COMMAND_ADAPTER_H */
