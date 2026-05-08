#ifndef PLATFORM_DRIVERS_SIMULATED_DRYER_DRIVER_H
#define PLATFORM_DRIVERS_SIMULATED_DRYER_DRIVER_H

#include "domain/ports/actuator_port.h"
#include "platform/drivers/simulated_driver_context.h"

/**
 * @file simulated_dryer_driver.h
 * @brief 声明风干仿真驱动绑定入口。
 */

/**
 * @brief 绑定风干仿真驱动。
 * @param actuator_port 执行机构端口，不能为空。
 * @param driver_context 仿真驱动上下文，不能为空。
 * @return 绑定成功返回 0，否则返回非 0。
 */
int simulated_dryer_driver_bind(actuator_port_t *actuator_port, simulated_driver_context_t *driver_context);

#endif
