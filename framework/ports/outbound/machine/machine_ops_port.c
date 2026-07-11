/**
 * @file    machine_ops_port.c
 * @brief   机型运行时操作端口注册表
 * @author  HUWANGWEI
 * @date    2026-07-11
 */

#include "framework/ports/outbound/machine/machine_ops_port.h"

static const machine_ops_t *s_ops;

void machine_ops_register(const machine_ops_t *ops)
{
    s_ops = ops;
}

const machine_ops_t *machine_ops_get(void)
{
    return s_ops;
}
