/**
 * @file    hal_io_port.c
 * @brief   HAL IO 端口注册表
 */

#include "ports/outbound/hal/hal_io_port.h"

static const hal_io_ops_t *s_ops;

void hal_io_register(const hal_io_ops_t *ops)
{
    s_ops = ops;
}

const hal_io_ops_t *hal_io_get_ops(void)
{
    return s_ops;
}
