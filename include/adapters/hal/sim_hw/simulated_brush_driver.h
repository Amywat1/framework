#ifndef ADAPTERS_HAL_SIM_HW_SIMULATED_BRUSH_DRIVER_H
#define ADAPTERS_HAL_SIM_HW_SIMULATED_BRUSH_DRIVER_H

#include "ports/hal/actuator_port.h"
#include "adapters/hal/sim_hw/simulated_driver_context.h"

int simulated_brush_driver_bind(actuator_port_t *actuator_port, simulated_driver_context_t *driver_context);

#endif
