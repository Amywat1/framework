/**
 * @file    snack_cloud_adapter.h
 * @brief   Snack MQTT 云端适配器（链路 + 属性上报 + 下行回复）
 * @author  HUWANGWEI
 * @date    2026-07-08
 */

#ifndef ADAPTERS_OUTBOUND_CLOUD_PROVIDERS_SNACK_SNACK_CLOUD_ADAPTER_H
#define ADAPTERS_OUTBOUND_CLOUD_PROVIDERS_SNACK_SNACK_CLOUD_ADAPTER_H

#include "common/point_table/point_table.h"
#include "common/sw_error.h"

#ifdef __cplusplus
extern "C" {
#endif

/** 常用 MQTT 心跳间隔（秒），供装配参考 */
#define SNACK_MQTT_HEARTBEAT_DEFAULT_S 15U

/**
 * @brief  注册 Snack 实现到 cloud_link_port。
 *
 * @note   本函数注册 ops，并把 bool 属性上行编码设为 0/1（阿里云 TSL 不接受 true/false）；
 *         不读取部署配置，也不初始化 MQTT。不清除已 configure 的凭证与 topic。
 */
void snack_cloud_adapter_register(void);

/**
 * @brief  配置 Snack MQTT 凭据、心跳与属性 topic。
 *
 * @param  product_key         产品密钥，非空。
 * @param  device_sn           设备序列号，非空。
 * @param  device_secret       设备密钥，非空。
 * @param  iot_instance        阿里云物联网实例 ID；NULL 或空串表示公共实例。
 * @param  topic_property_up   属性上报 topic，非空。
 * @param  topic_property_reply 属性下发应答 topic；NULL 或空串表示不回复。
 * @param  heartbeat_s         MQTT 心跳间隔（秒），须大于 0。
 * @retval SW_OK 配置成功。
 * @retval SW_ERR_PARAM 参数为空、心跳为 0 或长度超过内部缓冲区。
 */
sw_err_t snack_cloud_adapter_configure(const char *product_key,
                                       const char *device_sn,
                                       const char *device_secret,
                                       const char *iot_instance,
                                       const char *topic_property_up,
                                       const char *topic_property_reply,
                                       uint16_t    heartbeat_s);

/**
 * @brief  属性下发应答（供 cloud_json_install 的 reply 注入）
 * @note   未配置回复 topic 时静默跳过。
 */
sw_err_t snack_cloud_property_reply(const char *request_json, const point_apply_result_t *result);

#ifdef __cplusplus
}
#endif

#endif /* ADAPTERS_OUTBOUND_CLOUD_PROVIDERS_SNACK_SNACK_CLOUD_ADAPTER_H */
