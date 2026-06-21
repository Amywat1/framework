/**
 * @file    mqtt_command_parser.c
 * @brief   MQTT 命令解析器实现（纯 JSON→cmd_t，不依赖 MQTT SDK）
 * @author  HUWANGWEI
 * @date    2026-04-10
 */

#include "adapters/ui/mqtt_cmd/mqtt_command_parser.h"
#include "common/log.h"
#include "tools/cJSON.h"
#include <string.h>

bool mqtt_command_parse(const char *json_str, cmd_t *out_cmd)
{
    cJSON *root;
    cJSON *action_item;
    bool   ok = false;

    if ((json_str == NULL) || (out_cmd == NULL))
    {
        return false;
    }

    root = cJSON_Parse(json_str);
    if (root == NULL)
    {
        return false;
    }

    action_item = cJSON_GetObjectItem(root, MQTT_CMD_FIELD_ACTION);
    if (!cJSON_IsString(action_item) || (action_item->valuestring == NULL))
    {
        cJSON_Delete(root);
        return false;
    }

    const char *action = action_item->valuestring;
    out_cmd->type = CMD_NONE;

    if (strcmp(action, MQTT_CMD_ACTION_START_WASH) == 0)
    {
        out_cmd->type = CMD_START_WASH;
        /* 可选 mode 字段，默认 STANDARD */
        cJSON *mode_item = cJSON_GetObjectItem(root, MQTT_CMD_FIELD_MODE);
        out_cmd->payload.start_wash.mode =
            (cJSON_IsNumber(mode_item) && (mode_item->valuedouble >= 0.0))
            ? (wash_mode_t)(int)mode_item->valuedouble
            : WASH_MODE_STANDARD;
        ok = true;
    }
    else if (strcmp(action, MQTT_CMD_ACTION_STOP_WASH) == 0)
    {
        out_cmd->type = CMD_STOP_WASH;
        ok = true;
    }
    else if (strcmp(action, MQTT_CMD_ACTION_STOP_OPERATION) == 0)
    {
        out_cmd->type = CMD_STOP_OPERATION;
        ok = true;
    }
    else if (strcmp(action, MQTT_CMD_ACTION_RESUME_OPERATION) == 0)
    {
        out_cmd->type = CMD_RESUME_OPERATION;
        ok = true;
    }
    else if (strcmp(action, MQTT_CMD_ACTION_RESET_FAULT) == 0)
    {
        out_cmd->type = CMD_RESET_FAULT;
        ok = true;
    }
    else if (strcmp(action, MQTT_CMD_ACTION_HOME_DEVICE) == 0)
    {
        out_cmd->type = CMD_HOME_DEVICE;
        ok = true;
    }
    else
    {
        LOG_WARN("mqtt_parser: unknown action=%s", action);
    }

    cJSON_Delete(root);
    return ok;
}
