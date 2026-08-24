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

/**
 * @brief  注册 Snack 实现到 cloud_link_port。
 *
 * @note   本函数只注册 ops，不读取部署配置，也不初始化 MQTT。
 *         不清除已 configure 的凭证与 topic。
 */
void snack_cloud_adapter_register(void);

/**
 * @brief  配置 Snack MQTT 凭据与属性 topic。
 *
 * @param  product_key         产品密钥，非空。
 * @param  device_sn           设备序列号，非空。
 * @param  device_secret       设备密钥，非空。
 * @param  topic_property_up   属性上报 topic，非空。
 * @param  topic_property_reply 属性下发应答 topic；NULL 或空串表示不回复。
 * @retval SW_OK 配置成功。
 * @retval SW_ERR_PARAM 参数为空或长度超过内部缓冲区。
 */
sw_err_t snack_cloud_adapter_configure(const char *product_key,
                                       const char *device_sn,
                                       const char *device_secret,
                                       const char *topic_property_up,
                                       const char *topic_property_reply);

/**
 * @brief  属性下发应答（供 cloud_json_install 的 reply 注入）
 * @note   未配置回复 topic 时静默跳过。
 */
sw_err_t snack_cloud_property_reply(const char *request_json, const point_apply_result_t *result);

#ifdef __cplusplus
}
#endif

#endif /* ADAPTERS_OUTBOUND_CLOUD_PROVIDERS_SNACK_SNACK_CLOUD_ADAPTER_H */
