/**
 * @file    command_guard.c
 * @brief   外部命令同步准入校验实现
 * @author  HUWANGWEI
 * @date    2026-06-01
 */

#include "application/command_guard.h"
#include "service/dev_ctx/dev_ctx.h"
#include "domain/model/device_state.h"
#include "common/log.h"

sw_err_t command_guard_check(const cmd_t *cmd)
{
    device_context_t ctx;

    if (cmd == NULL)
    {
        return SW_ERR_PARAM;
    }

    ctx = dev_ctx_snapshot();

    switch (cmd->type)
    {
        case CMD_START_WASH:
            if (ctx.device_state != DEV_STATE_IDLE)
            {
                LOG_WARN("command_guard: START_WASH rejected (device=%d)",
                         (int)ctx.device_state);
                return SW_ERR_STATE;
            }
            return SW_OK;

        case CMD_STOP_WASH:
            if (ctx.device_state != DEV_STATE_RUNNING)
            {
                LOG_WARN("command_guard: STOP_WASH rejected (device=%d)",
                         (int)ctx.device_state);
                return SW_ERR_STATE;
            }
            return SW_OK;

        case CMD_STOP_OPERATION:
            if (ctx.device_state != DEV_STATE_IDLE)
            {
                LOG_WARN("command_guard: STOP_OPERATION rejected (device=%d)",
                         (int)ctx.device_state);
                return SW_ERR_STATE;
            }
            return SW_OK;

        case CMD_RESUME_OPERATION:
            if (ctx.device_state != DEV_STATE_STOP)
            {
                LOG_WARN("command_guard: RESUME_OPERATION rejected (device=%d)",
                         (int)ctx.device_state);
                return SW_ERR_STATE;
            }
            return SW_OK;

        case CMD_RESET_FAULT:
            if (ctx.device_state != DEV_STATE_FAULT)
            {
                LOG_WARN("command_guard: RESET_FAULT rejected (device=%d)",
                         (int)ctx.device_state);
                return SW_ERR_STATE;
            }
            return SW_OK;

        case CMD_HOME_DEVICE:
            if (ctx.device_state != DEV_STATE_FAULT)
            {
                LOG_WARN("command_guard: HOME_DEVICE rejected (device=%d)",
                         (int)ctx.device_state);
                return SW_ERR_STATE;
            }
            return SW_OK;

        default:
            LOG_WARN("command_guard: unknown cmd type=%d", (int)cmd->type);
            return SW_ERR_PARAM;
    }
}
