#include "platform/drivers/simulated_driver_context.h"

#include "domain/ports/actuator_port.h"

static int simulated_control_roof_brush(void *context, brush_mode_t brush_mode, int timeout_ms)
{
    simulated_driver_context_t *driver_context = (simulated_driver_context_t *)context;
    (void)brush_mode;
    (void)timeout_ms;
    driver_context->roof_brush_count += 1;
    return 0;
}

static int simulated_control_side_brush(void *context, brush_mode_t brush_mode, int timeout_ms)
{
    simulated_driver_context_t *driver_context = (simulated_driver_context_t *)context;
    (void)brush_mode;
    (void)timeout_ms;
    driver_context->side_brush_count += 1;
    return 0;
}

int simulated_brush_driver_bind(actuator_port_t *actuator_port, simulated_driver_context_t *driver_context)
{
    actuator_port->context = driver_context;
    actuator_port->control_roof_brush = simulated_control_roof_brush;
    actuator_port->control_side_brush = simulated_control_side_brush;
    return 0;
}

