/**
 * @file    m8_tsl_table.h
 * @brief   M8 物模型点位表接口
 * @author  HUWANGWEI
 * @date    2026-07-02
 *
 * @note    M8 专属：登记 M8 在阿里云控制台注册的物模型标识符与对应的
 *          get/set 实现。通用序列化/分发引擎见
 *          framework/adapters/outbound/cloud/tsl/tsl_point.h，
 *          新增/删除/修改点位只需要改 m8_tsl_table.c，不需要改通用引擎。
 *          m8_build_report_json / m8_tsl_command_dispatch 是把点位表接入
 *          aliyun_adapter.h 两个固定函数指针形状的适配函数，供
 *          projects/m8/wiring/wiring.c / project_hooks.c 注入使用。
 */

#ifndef MACHINES_M8_ADAPTERS_CLOUD_TSL_TABLE_H
#define MACHINES_M8_ADAPTERS_CLOUD_TSL_TABLE_H

#include "framework/adapters/outbound/cloud/tsl/tsl_point.h"
#include "framework/ports/outbound/cloud/report/report_port.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief  获取 M8 物模型点位表
 * @param  out_count  输出点位数量；传 NULL 时不输出
 * @return 点位数组首地址（静态只读，调用方不得修改）
 */
const tsl_point_t *m8_tsl_table(size_t *out_count);

/**
 * @brief  将 M8 物模型点位表序列化为上报 JSON 字符串
 * @param  p         预留参数，当前实现忽略（各点位 get() 自行读取数据源）
 * @param  buf       输出缓冲区
 * @param  buf_size  缓冲区大小
 * @retval SW_OK       序列化成功
 * @retval SW_ERR_PARAM 缓冲区不足或序列化失败
 * @note   匹配 aliyun_adapter.h 的 aliyun_report_builder_fn_t 形状，
 *         由 wiring.c 注入 aliyun_report_adapter_register()
 */
sw_err_t m8_build_report_json(const cloud_report_payload_t *p,
                               char *buf, size_t buf_size);

/**
 * @brief  分发一条云端下行属性设置消息到 M8 物模型点位表
 * @param  json_str  {"标识符":值, ...} 形式的 JSON 字符串
 * @note   匹配 aliyun_adapter.h 的 aliyun_cmd_dispatch_fn_t 形状，
 *         由 bootstrap.c 注入 aliyun_command_adapter_init()
 */
void m8_tsl_command_dispatch(const char *json_str);

#ifdef __cplusplus
}
#endif

#endif /* MACHINES_M8_ADAPTERS_CLOUD_TSL_TABLE_H */
