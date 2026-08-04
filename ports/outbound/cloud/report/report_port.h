/**
 * @file    report_port.h
 * @brief   云端状态上报端口接口
 * @author  HUWANGWEI
 * @date    2026-04-10
 *
 * @note    application/report_scheduler 通过此接口触发上报，不感知具体云平台与物模型字段。
 *          JSON 内容由 cloud_model_build_* 构建。
 */

#ifndef PORTS_OUTBOUND_CLOUD_REPORT_REPORT_PORT_H
#define PORTS_OUTBOUND_CLOUD_REPORT_REPORT_PORT_H

#ifdef __cplusplus
extern "C" {
#endif

#include "common/sw_error.h"

#include <stddef.h>

/**
 * @brief  云端上报操作表
 */
typedef struct {
    /**
     * @brief  发布一次全量属性上报
     * @retval SW_OK         发送成功
     * @retval SW_ERR_COMM   云端未连接或发送失败
     */
    sw_err_t (*publish_properties)(void);

    /**
     * @brief  发布指定点位的增量属性上报
     * @param  ids    物模型属性 id 数组
     * @param  count  id 数量
     * @retval SW_OK         发送成功
     * @retval SW_ERR_COMM   云端未连接或发送失败
     * @note   若 provider 未实现增量 builder，可回退为全量上报
     */
    sw_err_t (*publish_properties_delta)(const char *const *ids, size_t count);
} cloud_report_ops_t;

/**
 * @brief  注册云端上报 port 实现
 */
sw_err_t cloud_report_register(const cloud_report_ops_t *ops);

/**
 * @brief  获取已注册的云端上报 port 实现
 * @retval NULL  尚未注册（仿真或未启用云端）
 */
const cloud_report_ops_t *cloud_report_get_ops(void);

#ifdef __cplusplus
}
#endif

#endif /* PORTS_OUTBOUND_CLOUD_REPORT_REPORT_PORT_H */
