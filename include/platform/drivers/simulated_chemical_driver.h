#ifndef PLATFORM_DRIVERS_SIMULATED_CHEMICAL_DRIVER_H
#define PLATFORM_DRIVERS_SIMULATED_CHEMICAL_DRIVER_H

#include "domain/ports/actuator_port.h"
#include "platform/drivers/simulated_driver_context.h"

int simulated_chemical_driver_bind(actuator_port_t *actuator_port, simulated_driver_context_t *driver_context);

#endif

