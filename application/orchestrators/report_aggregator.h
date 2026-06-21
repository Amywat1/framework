/**
 * @file    report_aggregator.h
 * @brief   云端状态上报聚合器接口
 * @author  HUWANGWEI
 * @date    2026-04-10
 *
 * @note    定时读取 dev_ctx_snapshot()，通过 report_port 接口上报。
 *          订阅 EVT_CLOUD_CONNECTED/DISCONNECTED 更新 dev_ctx 云端连接状态。
 */

#ifndef APPLICATION_REPORT_AGGREGATOR_H
#define APPLICATION_REPORT_AGGREGATOR_H

#ifdef __cplusplus
extern "C" {
#endif

#include "common/sw_error.h"

/**
 * @brief  初始化上报聚合器（注册 cloud_thread、订阅云端连接事件）
 */
sw_err_t report_aggregator_init(void);

#ifdef __cplusplus
}
#endif

#endif /* APPLICATION_REPORT_AGGREGATOR_H */
