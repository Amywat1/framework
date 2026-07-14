/**
 * @file    snack_cloud_link_adapter.h
 * @brief   Snack MQTT 云端链路适配器接口
 * @author  HUWANGWEI
 * @date    2026-07-08
 */

#ifndef FRAMEWORK_ADAPTERS_OUTBOUND_CLOUD_PROVIDERS_SNACK_CLOUD_LINK_ADAPTER_H
#define FRAMEWORK_ADAPTERS_OUTBOUND_CLOUD_PROVIDERS_SNACK_CLOUD_LINK_ADAPTER_H

#include "common/sw_error.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief  注册 Snack 实现到 cloud_link_port。
 *
 * @note   本函数只注册 ops，不读取部署配置，也不初始化 MQTT。
 */
void snack_cloud_link_adapter_register(void);

/**
 * @brief  配置 Snack MQTT 连接凭据。
 *
 * @param  product_key 产品密钥，必须为非空字符串。
 * @param  device_sn 设备序列号，必须为非空字符串。
 * @param  device_secret 设备密钥，必须为非空字符串。
 * @retval SW_OK 配置成功。
 * @retval SW_ERR_PARAM 参数为空或长度超过内部缓冲区。
 * @note   须在 cloud_link_port 的 init() 前调用。
 */
sw_err_t snack_cloud_link_adapter_configure(const char *product_key, const char *device_sn, const char *device_secret);

#ifdef __cplusplus
}
#endif

#endif /* FRAMEWORK_ADAPTERS_OUTBOUND_CLOUD_PROVIDERS_SNACK_CLOUD_LINK_ADAPTER_H */
