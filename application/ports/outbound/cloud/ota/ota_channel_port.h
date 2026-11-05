/**
 * @file    ota_channel_port.h
 * @brief   云端 OTA 通道端口：上报版本并订阅升级任务、查询下载忙碌
 *
 * @note    云 provider 实现，交给 application/ota_gate。何时上报、报哪一串版本由门闩决定。
 */

#ifndef APPLICATION_PORTS_OUTBOUND_CLOUD_OTA_OTA_CHANNEL_PORT_H
#define APPLICATION_PORTS_OUTBOUND_CLOUD_OTA_OTA_CHANNEL_PORT_H

#ifdef __cplusplus
extern "C" {
#endif

#include "common/sw_error.h"

#include <stdbool.h>

/**
 * @brief  云端 OTA 通道
 */
typedef struct {
    /**
     * @brief  上报当前版本并订阅升级任务
     * @param  version 门闩登记的版本串
     * @return SW_OK 已上报；其它值下次事件再试
     * @note   必填。仅在云在线且处于允许态时调用。
     */
    sw_err_t (*report)(const char *version);

    /**
     * @brief  通道是否正在下载或处理升级包
     * @note   可空。为真时拒绝开洗。
     */
    bool (*busy)(void);
} ota_channel_ops_t;

#ifdef __cplusplus
}
#endif

#endif /* APPLICATION_PORTS_OUTBOUND_CLOUD_OTA_OTA_CHANNEL_PORT_H */
