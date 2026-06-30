/**
 * @file    command_parser.h
 * @brief   命令解析器接口（JSON 文本 → cmd_t，平台无关）
 * @author  HUWANGWEI
 * @date    2026-04-10
 *
 * @note    不依赖任何云平台或传输层头文件，可被 MQTT / CLI / BLE 等任意命令来源复用。
 */

#ifndef ADAPTERS_COMMAND_PARSER_H
#define ADAPTERS_COMMAND_PARSER_H

#ifdef __cplusplus
extern "C" {
#endif

#include "ports/cloud/command_port.h"
#include <stdbool.h>

/* -------------------------------------------------------------------------
 * 命令 JSON 字段名与 action 值
 * ------------------------------------------------------------------------- */
#define CMD_JSON_FIELD_ACTION             "action"
#define CMD_JSON_FIELD_MODE               "mode"

#define CMD_JSON_ACTION_START_WASH        "startWash"
#define CMD_JSON_ACTION_STOP_WASH         "stopWash"
#define CMD_JSON_ACTION_STOP_OPERATION    "stopOperation"
#define CMD_JSON_ACTION_RESUME_OPERATION  "resumeOperation"
#define CMD_JSON_ACTION_RESET_FAULT       "resetFault"
#define CMD_JSON_ACTION_HOME_DEVICE       "homeDevice"

/**
 * @brief  将 JSON 字符串解析为 cmd_t
 * @param  json_str  原始 JSON 字符串
 * @param  out_cmd   输出命令结构体（仅在返回 true 时有效）
 * @retval true  解析成功
 * @retval false 格式错误或未知命令
 */
bool command_parse(const char *json_str, cmd_t *out_cmd);

#ifdef __cplusplus
}
#endif

#endif /* ADAPTERS_COMMAND_PARSER_H */
