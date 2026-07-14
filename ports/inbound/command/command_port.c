/**
 * @file    command_port.c
 * @brief   设备命令端口注册表
 */

#include "ports/inbound/command/command_port.h"

static const device_command_port_ops_t *s_ops;

void device_command_port_register(const device_command_port_ops_t *ops)
{
    s_ops = ops;
}

const device_command_port_ops_t *device_command_port_get_ops(void)
{
    return s_ops;
}
