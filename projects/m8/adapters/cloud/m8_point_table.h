/**
 * @file    m8_point_table.h
 * @brief   M8 云端点位表接口
 * @author  HUWANGWEI
 * @date    2026-07-02
 *
 * @note    M8 专属：登记云端物模型标识符与对应的 get/set 实现。通用 JSON 引擎见
 *          framework/common/point_table/point_table.h，新增/删除/修改点位只需改
 *          m8_point_table.c。m8_cloud_report_json / m8_cloud_command_dispatch
 *          是把点位表接入 snack_cloud_*_adapter 固定函数指针形状的适配函数。
 */

#ifndef PROJECTS_M8_ADAPTERS_CLOUD_M8_POINT_TABLE_H
#define PROJECTS_M8_ADAPTERS_CLOUD_M8_POINT_TABLE_H

#include "framework/common/point_table/point_table.h"
#include "framework/ports/outbound/cloud/report/report_port.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief  获取 M8 云端点位表
 * @param  out_count  输出点位数量；传 NULL 时不输出
 * @return 点位数组首地址（静态只读，调用方不得修改）
 */
const point_table_entry_t *m8_point_table(size_t *out_count);

/**
 * @brief  将 M8 点位表序列化为云端上报 JSON 字符串
 * @param  p         预留参数，当前实现忽略（各点位 get() 自行读取数据源）
 * @param  buf       输出缓冲区
 * @param  buf_size  缓冲区大小
 * @retval SW_OK       序列化成功
 * @retval SW_ERR_PARAM 缓冲区不足或序列化失败
 * @note   匹配 snack_cloud_report_adapter.h 的 snack_cloud_report_builder_fn_t 形状
 */
sw_err_t m8_cloud_report_json(const cloud_report_payload_t *p,
                               char *buf, size_t buf_size);

/**
 * @brief  分发一条云端下行 JSON 到 M8 点位表
 * @param  json_str  {"标识符":值, ...} 形式的 JSON 字符串
 * @note   匹配 snack_cloud_command_adapter.h 的 snack_cloud_cmd_dispatch_fn_t 形状
 */
void m8_cloud_command_dispatch(const char *json_str);

#ifdef __cplusplus
}
#endif

#endif /* PROJECTS_M8_ADAPTERS_CLOUD_M8_POINT_TABLE_H */
