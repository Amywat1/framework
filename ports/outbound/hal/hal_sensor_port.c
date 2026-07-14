/**
 * @file    hal_sensor_port.c
 * @brief   HAL 传感器端口注册表
 */

#include "ports/outbound/hal/hal_sensor_port.h"

static const hal_sensor_ops_t *s_ops;

void hal_sensor_register(const hal_sensor_ops_t *ops)
{
    s_ops = ops;
}

const hal_sensor_ops_t *hal_sensor_get_ops(void)
{
    return s_ops;
}
