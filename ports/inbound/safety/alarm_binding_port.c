/**
 * @file    alarm_binding_port.c
 * @brief   安全报警绑定入站端口注册表
 */

#include "ports/inbound/safety/alarm_binding_port.h"

static const alarm_binding_ops_t *s_ops;

void alarm_binding_register(const alarm_binding_ops_t *ops)
{
    s_ops = ops;
}

const alarm_binding_ops_t *alarm_binding_get_ops(void)
{
    return s_ops;
}
