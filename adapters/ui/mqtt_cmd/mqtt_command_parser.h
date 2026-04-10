/**
 * @file    mqtt_command_parser.h
 * @brief   MQTT 命令解析器接口（纯 JSON 文本 → cmd_t）
 * @author  胡望伟
 * @date    2026-04-10
 *
 * @note    不依赖 MQTT SDK 及任何云平台头文件，可被任意命令来源复用。
 *          JSON 字段名和 action 值定义在本头文件，不引用 aliyun_topics.h。
 */

#ifndef ADAPTERS_UI_MQTT_CMD_PARSER_H
#define ADAPTERS_UI_MQTT_CMD_PARSER_H

#ifdef __cplusplus
extern "C" {
#endif

#include "domain/model/command.h"
#include <stdbool.h>

/* -------------------------------------------------------------------------
 * 命令 JSON 字段名（平台无关）
 * ------------------------------------------------------------------------- */
#define MQTT_CMD_FIELD_ACTION             "action"
#define MQTT_CMD_FIELD_MODE               "mode"

#define MQTT_CMD_ACTION_START_WASH        "startWash"
#define MQTT_CMD_ACTION_STOP_WASH         "stopWash"
#define MQTT_CMD_ACTION_STOP_OPERATION    "stopOperation"
#define MQTT_CMD_ACTION_RESUME_OPERATION  "resumeOperation"
#define MQTT_CMD_ACTION_RESET_FAULT       "resetFault"
#define MQTT_CMD_ACTION_HOME_DEVICE       "homeDevice"

/**
 * @brief  将 JSON 字符串解析为 cmd_t
 * @param  json_str  原始 JSON 字符串
 * @param  out_cmd   输出命令结构体（仅在返回 true 时有效）
 * @retval true  解析成功
 * @retval false 格式错误或未知命令
 */
bool mqtt_command_parse(const char *json_str, cmd_t *out_cmd);

#ifdef __cplusplus
}
#endif

#endif /* ADAPTERS_UI_MQTT_CMD_PARSER_H */
