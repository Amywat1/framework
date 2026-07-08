/**
 * @file    snack_cloud_link_adapter.h
 * @brief   Snack MQTT 云端链路适配器接口
 * @author  HUWANGWEI
 * @date    2026-07-08
 */

#ifndef FRAMEWORK_ADAPTERS_OUTBOUND_CLOUD_PROVIDERS_SNACK_CLOUD_LINK_ADAPTER_H
#define FRAMEWORK_ADAPTERS_OUTBOUND_CLOUD_PROVIDERS_SNACK_CLOUD_LINK_ADAPTER_H

#include "framework/common/sw_error.h"

#ifdef __cplusplus
extern "C" {
#endif

void snack_cloud_link_adapter_register(void);
sw_err_t snack_cloud_link_adapter_init(void);
sw_err_t snack_cloud_link_adapter_start(void);

#ifdef __cplusplus
}
#endif

#endif /* FRAMEWORK_ADAPTERS_OUTBOUND_CLOUD_PROVIDERS_SNACK_CLOUD_LINK_ADAPTER_H */
