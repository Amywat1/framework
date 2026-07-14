/**
 * @file    property_port.c
 * @brief   云端属性下发端口注册表
 */

#include "ports/inbound/cloud/property/property_port.h"

static const cloud_property_ops_t *s_ops;

void cloud_property_register(const cloud_property_ops_t *ops)
{
    s_ops = ops;
}

const cloud_property_ops_t *cloud_property_get_ops(void)
{
    return s_ops;
}
