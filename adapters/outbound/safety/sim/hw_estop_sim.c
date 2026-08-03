/**
 * @file    hw_estop_sim.c
 * @brief   仿真硬件急停输入状态
 *
 * @note    只维护仿真急停状态；接入安全端口由 safety_sim.c 统一注册，
 *          本文件不再直接定义 hw_estop_port_is_active。
 */

#include "adapters/outbound/safety/sim/hw_estop_sim.h"

#include <stdatomic.h>

static atomic_bool s_hw_estop_sim_active = false;

void hw_estop_sim_set_active(bool active)
{
    atomic_store(&s_hw_estop_sim_active, active);
}

bool hw_estop_sim_get_active(void)
{
    return atomic_load(&s_hw_estop_sim_active);
}
