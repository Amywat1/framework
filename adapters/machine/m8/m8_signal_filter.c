/**
 * @file    m8_signal_filter.c
 * @brief   M8 滤波信号查询（滤波由 hal_sensor 完成）
 * @author  HUWANGWEI
 * @date    2026-04-13
 */

#include "adapters/machine/m8/m8_signal_filter.h"
#include "ports/hal/hal_sensor_port.h"

void m8_signal_filter_tick(void)
{
    const hal_sensor_ops_t *sensor = hal_sensor_get_ops();

    if ((sensor != NULL) && (sensor->tick != NULL))
    {
        sensor->tick();
    }
}

bool m8_signal_is_active(m8_signal_id_t sig_id)
{
    const hal_sensor_ops_t *sensor = hal_sensor_get_ops();

    if (((int)sig_id < 0) || ((int)sig_id >= M8_SIGNAL_TABLE_SIZE))
    {
        return false;
    }

    if ((sensor == NULL) || (sensor->is_active == NULL))
    {
        return false;
    }

    return sensor->is_active((hal_sensor_channel_t)sig_id);
}
