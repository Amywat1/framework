#include <string.h>

#include "platform/drivers/simulated_driver_context.h"

#include "domain/ports/sensor_port.h"

void simulated_driver_context_init(simulated_driver_context_t *driver_context)
{
    memset(driver_context, 0, sizeof(*driver_context));
    driver_context->sensor_snapshot.vehicle_present = true;
    driver_context->sensor_snapshot.vehicle_allowed = true;
    driver_context->sensor_snapshot.safety_ok = true;
    driver_context->sensor_snapshot.resource_ok = true;
    driver_context->sensor_snapshot.position_ok = true;
    driver_context->sensor_snapshot.estop_active = false;
}

static int simulated_read_snapshot(void *context, sensor_snapshot_t *snapshot)
{
    simulated_driver_context_t *driver_context = (simulated_driver_context_t *)context;
    *snapshot = driver_context->sensor_snapshot;
    return 0;
}

int simulated_sensor_driver_bind(sensor_port_t *sensor_port, simulated_driver_context_t *driver_context)
{
    sensor_port->context = driver_context;
    sensor_port->read_snapshot = simulated_read_snapshot;
    return 0;
}

