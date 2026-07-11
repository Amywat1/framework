/**
 * @file    hw_estop_sim.c
 * @brief   仿真硬件急停输入实现（强符号覆盖 ports 弱实现）
 */

#include "adapters/outbound/safety/sim/hw_estop_sim.h"

#include "ports/outbound/safety/hw_estop_port.h"

#include <stdatomic.h>

static atomic_bool s_estop_active = false;

void hw_estop_sim_set_active(bool active)
{
    atomic_store(&s_estop_active, active);
}

bool hw_estop_sim_get_active(void)
{
    return atomic_load(&s_estop_active);
}

bool hw_estop_port_is_active(void)
{
    return hw_estop_sim_get_active();
}
