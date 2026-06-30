/**
 * @file    report_builder.h
 * @brief   M8 云端上报 JSON 构建器接口
 * @author  HUWANGWEI
 * @date    2026-04-10
 *
 * @note    定义 M8 机型云端上报协议的 JSON 字段名与构建函数。
 *          通过函数指针注入 aliyun_report_adapter_register()，
 *          使云端适配器不感知具体字段名。
 */

#ifndef MACHINES_M8_ADAPTERS_CLOUD_REPORT_BUILDER_H
#define MACHINES_M8_ADAPTERS_CLOUD_REPORT_BUILDER_H

#include "ports/cloud/report_port.h"
#include "common/sw_error.h"
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* -------------------------------------------------------------------------
 * M8 云端上报协议 JSON 字段名
 * ------------------------------------------------------------------------- */
#define M8_REPORT_FIELD_DEV_STATE    "devState"
#define M8_REPORT_FIELD_WASH_MODE    "washMode"
#define M8_REPORT_FIELD_GANTRY_POS   "gantryPos"
#define M8_REPORT_FIELD_HAS_ALARM    "hasAlarm"
#define M8_REPORT_FIELD_ALARM_CODE   "alarmCode"
#define M8_REPORT_FIELD_CLOUD_CONN   "cloudConn"

/**
 * @brief  将 cloud_report_payload_t 序列化为 M8 上报 JSON 字符串
 * @param  p         上报载荷
 * @param  buf       输出缓冲区
 * @param  buf_size  缓冲区大小
 * @retval SW_OK       序列化成功
 * @retval SW_ERR_PARAM 缓冲区不足
 */
sw_err_t m8_build_report_json(const cloud_report_payload_t *p,
                               char *buf, size_t buf_size);

#ifdef __cplusplus
}
#endif

#endif /* MACHINES_M8_ADAPTERS_CLOUD_REPORT_BUILDER_H */
