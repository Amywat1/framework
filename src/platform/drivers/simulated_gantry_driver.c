#include "platform/drivers/simulated_driver_context.h"

#include "domain/ports/actuator_port.h"

static int simulated_move_gantry(void *context, gantry_motion_mode_t motion_mode, int traverse_count, int timeout_ms)
{
    simulated_driver_context_t *driver_context = (simulated_driver_context_t *)context;
    (void)motion_mode;
    (void)traverse_count;
    (void)timeout_ms;
    driver_context->move_count += 1;
    return 0;
}

int simulated_gantry_driver_bind(actuator_port_t *actuator_port, simulated_driver_context_t *driver_context)
{
    actuator_port->context = driver_context;
    actuator_port->move_gantry = simulated_move_gantry;
    return 0;
}

