/**
 * @file    command_bridge.c
 * @brief   命令端口桥接适配器实现
 * @author  胡望伟
 * @date    2026-04-10
 */

#include "adapters/ui/command_bridge.h"
#include "ports/cloud/command_port.h"
#include "core/event_bus/event_bus.h"
#include "common/event_types.h"
#include "common/log.h"

/* -------------------------------------------------------------------------
 * ops 实现
 * ------------------------------------------------------------------------- */
static sw_err_t bridge_inject(const cmd_t *cmd)
{
    if (cmd == NULL)
    {
        return SW_ERR_PARAM;
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

static void bridge_register_cb(command_inject_cb_t cb)
{
    /* 本实现直接写 event_bus，不使用回调机制；保留接口兼容性 */
    (void)cb;
}

static const command_port_ops_t s_ops = {
    .register_cb = bridge_register_cb,
    .inject      = bridge_inject,
};

void command_bridge_register(void)
{
    command_port_register(&s_ops);
    LOG_INFO("command_bridge: registered");
}
