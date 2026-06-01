#ifndef ADAPTERS_HAL_SIM_HW_SIMULATED_SENSOR_DRIVER_H
#define ADAPTERS_HAL_SIM_HW_SIMULATED_SENSOR_DRIVER_H

#include "ports/hal/sensor_port.h"
#include "adapters/hal/sim_hw/simulated_driver_context.h"

int simulated_sensor_driver_bind(sensor_port_t *sensor_port, simulated_driver_context_t *driver_context);

#endif
