#ifndef PLATFORM_DRIVERS_SIMULATED_SENSOR_DRIVER_H
#define PLATFORM_DRIVERS_SIMULATED_SENSOR_DRIVER_H

#include "domain/ports/sensor_port.h"
#include "platform/drivers/simulated_driver_context.h"

int simulated_sensor_driver_bind(sensor_port_t *sensor_port, simulated_driver_context_t *driver_context);

#endif
