/**
 * @file    m8_cloud_device_cmd.c
 * @brief   M8 云端设备生命周期命令 handler 实现
 * @author  HUWANGWEI
 * @date    2026-07-08
 */

#include "projects/m8/adapters/cloud/m8_cloud_device_cmd.h"
#include "framework/ports/inbound/command/command_port.h"
#include "framework/common/log.h"
#include <string.h>

sw_err_t m8_device_cmd_submit(cmd_type_t type)
{
    const command_port_ops_t *cp = command_port_get_ops();
    cmd_t                     cmd;

    if (cp == NULL)
    {
        LOG_WARN("m8_device_cmd: command_port not registered");
        return SW_ERR_NOT_INIT;
    }

    memset(&cmd, 0, sizeof(cmd));
    cmd.type = type;
    return cp->inject(&cmd);
}

sw_err_t m8_cloud_set_cmd_home(const point_value_t *in)
{
    return in->b ? m8_device_cmd_submit(CMD_HOME_DEVICE) : SW_OK;
}

sw_err_t m8_cloud_set_cmd_custom_stop(const point_value_t *in)
{
    return in->b ? m8_device_cmd_submit(CMD_STOP_WASH) : SW_OK;
}
