/**
 * @file    event_bridge.c
 * @brief   命令端口桥接适配器实现
 * @author  HUWANGWEI
 * @date    2026-04-10
 */

#include "adapters/command/event_bridge.h"
#include "application/command_guard.h"
#include "ports/cloud/command_port.h"
#include "infrastructure/event_bus/event_bus.h"
#include "common/event_types.h"
#include "common/log.h"

/* -------------------------------------------------------------------------
 * ops 实现
 * ------------------------------------------------------------------------- */
static sw_err_t bridge_inject(const cmd_t *cmd)
{
    sw_err_t ret;

    if (cmd == NULL)
    {
        return SW_ERR_PARAM;
    }

    ret = command_guard_check(cmd);
    if (ret != SW_OK)
    {
        return ret;
    }

    switch (cmd->type)
    {
        case CMD_START_WASH:
            return event_publish(EVT_CMD_ORDER,
                                 (uint32_t)cmd->payload.start_wash.mode);
        case CMD_STOP_WASH:
            return event_publish(EVT_CMD_STOP_WASH, 0U);
        case CMD_STOP_OPERATION:
            return event_publish(EVT_CMD_STOP_OPERATION, 0U);
        case CMD_RESUME_OPERATION:
            return event_publish(EVT_CMD_RESUME_OPERATION, 0U);
        case CMD_RESET_FAULT:
            return event_publish(EVT_CMD_RESET_FAULT, 0U);
        case CMD_HOME_DEVICE:
            return event_publish(EVT_CMD_HOME_DEVICE, 0U);
        default:
            LOG_WARN("command_bridge: unknown cmd type=%d", (int)cmd->type);
            return SW_ERR_PARAM;
    }
}

static const command_port_ops_t s_ops = {
    .inject = bridge_inject,
};

void command_bridge_register(void)
{
    command_port_register(&s_ops);
    LOG_INFO("command_bridge: registered");
}
