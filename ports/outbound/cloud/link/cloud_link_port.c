/**
 * @file    cloud_link_port.c
 * @brief   云端链路端口注册表
 */

#include "ports/outbound/cloud/link/cloud_link_port.h"

static const cloud_link_ops_t *s_ops;

void cloud_link_register(const cloud_link_ops_t *ops)
{
    s_ops = ops;
}

const cloud_link_ops_t *cloud_link_get_ops(void)
{
    return s_ops;
}
