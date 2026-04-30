#include <string.h>

#include "platform/drivers/simulated_driver_context.h"

#include "domain/ports/actuator_port.h"

static int simulated_dose_chemical(void *context, const char *channel_id, int duration_ms, int retry_limit, int timeout_ms)
{
    simulated_driver_context_t *driver_context = (simulated_driver_context_t *)context;
    (void)duration_ms;
    (void)retry_limit;
    (void)timeout_ms;
    if (driver_context->sensor_snapshot.resource_ok == false && channel_id != 0 && strcmp(channel_id, "foam") == 0) {
        return -1;
    }
    driver_context->chemical_count += 1;
    return 0;
}

static int simulated_stop_all(void *context, int timeout_ms)
{
    simulated_driver_context_t *driver_context = (simulated_driver_context_t *)context;
    (void)timeout_ms;
    driver_context->stop_count += 1;
    return 0;
}

int simulated_chemical_driver_bind(actuator_port_t *actuator_port, simulated_driver_context_t *driver_context)
{
    actuator_port->context = driver_context;
    actuator_port->dose_chemical = simulated_dose_chemical;
    actuator_port->stop_all = simulated_stop_all;
    return 0;
}

