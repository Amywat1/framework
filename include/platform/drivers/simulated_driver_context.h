#ifndef PLATFORM_DRIVERS_SIMULATED_DRIVER_CONTEXT_H
#define PLATFORM_DRIVERS_SIMULATED_DRIVER_CONTEXT_H

#include "domain/ports/sensor_port.h"

typedef struct simulated_driver_context_t {
    sensor_snapshot_t sensor_snapshot;
    int move_count;
    int roof_brush_count;
    int side_brush_count;
    int chemical_count;
    int stop_count;
} simulated_driver_context_t;

void simulated_driver_context_init(simulated_driver_context_t *driver_context);

#endif

