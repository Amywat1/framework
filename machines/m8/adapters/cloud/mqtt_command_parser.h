/**
 * @file    mqtt_command_parser.h
 * @brief   M8 MQTT 命令解析器接口（JSON action 字符串 → cmd_t）
 * @author  HUWANGWEI
 * @date    2026-04-10
 *
 * @note    定义 M8 机型云端命令协议的 action 字段值与解析函数。
 *          通过函数指针注入 aliyun_command_adapter_init()，
 *          使云端适配器不感知具体指令名。
 */

#ifndef MACHINES_M8_ADAPTERS_CLOUD_MQTT_COMMAND_PARSER_H
#define MACHINES_M8_ADAPTERS_CLOUD_MQTT_COMMAND_PARSER_H

#include "ports/cloud/command_port.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* -------------------------------------------------------------------------
 * M8 云端命令协议字段名与 action 值
 * ------------------------------------------------------------------------- */
#define M8_CMD_JSON_FIELD_ACTION             "action"
#define M8_CMD_JSON_FIELD_MODE               "mode"

#define M8_CMD_JSON_ACTION_START_WASH        "startWash"
#define M8_CMD_JSON_ACTION_STOP_WASH         "stopWash"
#define M8_CMD_JSON_ACTION_STOP_OPERATION    "stopOperation"
#define M8_CMD_JSON_ACTION_RESUME_OPERATION  "resumeOperation"
#define M8_CMD_JSON_ACTION_RESET_FAULT       "resetFault"
#define M8_CMD_JSON_ACTION_HOME_DEVICE       "homeDevice"

/**
 * @brief  将 JSON 字符串解析为 cmd_t（M8 机型命令协议）
 * @param  json_str  原始 JSON 字符串
 * @param  out_cmd   输出命令结构体（仅在返回 true 时有效）
 * @retval true  解析成功
 * @retval false 格式错误或未知命令
 */
bool m8_mqtt_command_parse(const char *json_str, cmd_t *out_cmd);

#ifdef __cplusplus
}
#endif

#endif /* MACHINES_M8_ADAPTERS_CLOUD_MQTT_COMMAND_PARSER_H */
