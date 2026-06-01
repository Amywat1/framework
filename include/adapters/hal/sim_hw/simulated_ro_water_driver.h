#ifndef ADAPTERS_HAL_SIM_HW_SIMULATED_RO_WATER_DRIVER_H
#define ADAPTERS_HAL_SIM_HW_SIMULATED_RO_WATER_DRIVER_H

#include "ports/hal/actuator_port.h"
#include "adapters/hal/sim_hw/simulated_driver_context.h"

/**
 * @file simulated_ro_water_driver.h
 * @brief 声明 RO 水仿真驱动绑定入口。
 */

/**
 * @brief 绑定 RO 水仿真驱动。
 * @param actuator_port 执行机构端口，不能为空。
 * @param driver_context 仿真驱动上下文，不能为空。
 * @return 绑定成功返回 0，否则返回非 0。
 */
int simulated_ro_water_driver_bind(actuator_port_t *actuator_port, simulated_driver_context_t *driver_context);

#endif
