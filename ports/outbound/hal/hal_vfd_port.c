/**
 * @file    hal_vfd_port.c
 * @brief   HAL 变频器端口注册表
 */

#include "ports/outbound/hal/hal_vfd_port.h"

static const hal_vfd_ops_t *s_ops;

void hal_vfd_register(const hal_vfd_ops_t *ops)
{
    s_ops = ops;
}

const hal_vfd_ops_t *hal_vfd_get_ops(void)
{
    return s_ops;
}
