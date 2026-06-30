/**
 * @file    aliyun_adapter.h
 * @brief   阿里云 MQTT 适配器接口（命令下行 + 状态上报）
 * @author  HUWANGWEI
 * @date    2026-04-10
 */

#ifndef ADAPTERS_CLOUD_ALIYUN_ADAPTER_H
#define ADAPTERS_CLOUD_ALIYUN_ADAPTER_H

#include "ports/cloud/command_port.h"
#include "ports/cloud/report_port.h"
#include "common/sw_error.h"
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/** 命令解析器函数指针类型：JSON 字符串 → cmd_t */
typedef bool (*aliyun_cmd_parser_fn_t)(const char *json, cmd_t *out_cmd);

/** 上报 JSON 构建器函数指针类型：cloud_report_payload_t → JSON 字符串 */
typedef sw_err_t (*aliyun_report_builder_fn_t)(const cloud_report_payload_t *p,
                                                char *buf, size_t buf_size);

/**
 * @brief  初始化阿里云 MQTT 连接并注册命令下行回调
 * @param  parser  命令解析函数，由调用方注入（机型特有的 action→cmd_t 映射）
 * @retval true   MQTT 连接成功
 * @retval false  连接失败，进入离线模式
 */
bool aliyun_command_adapter_init(aliyun_cmd_parser_fn_t parser);

/**
 * @brief  注册阿里云状态上报实现到 cloud_report_port
 * @param  builder  上报 JSON 构建函数，由调用方注入（机型特有的字段名与格式）
 * @note   须在 wiring 阶段调用，先于任何上报操作
 */
void aliyun_report_adapter_register(aliyun_report_builder_fn_t builder);

#ifdef __cplusplus
}
#endif

#endif /* ADAPTERS_CLOUD_ALIYUN_ADAPTER_H */
