#include "platform/drivers/simulated_chemical_driver.h"

static int simulated_set_chemical_enabled(void *context, bool enabled, int timeout_ms)
{
    simulated_driver_context_t *driver_context = (simulated_driver_context_t *)context;
    (void)timeout_ms;
    if (driver_context->chemical_set_command_should_fail)
    {
        return -1;
    }
    driver_context->chemical_enabled = enabled;
    driver_context->chemical_command_count += 1;
    driver_context->runtime_snapshot.actuator_feedback.chemical_closed = !enabled;
    return 0;
}

static int simulated_stop_chemical(void *context, int timeout_ms)
{
    simulated_driver_context_t *driver_context = (simulated_driver_context_t *)context;
    (void)timeout_ms;
    if (driver_context->chemical_stop_command_should_fail)
    {
        return -1;
    }
    driver_context->chemical_stop_command_count += 1;
    driver_context->chemical_enabled = false;
    driver_context->runtime_snapshot.actuator_feedback.chemical_closed =
        driver_context->chemical_close_feedback_available;
    return 0;
}

int simulated_chemical_driver_bind(actuator_port_t *actuator_port, simulated_driver_context_t *driver_context)
{
    actuator_port->context = driver_context;
    actuator_port->set_chemical_enabled = simulated_set_chemical_enabled;
    actuator_port->stop_chemical = simulated_stop_chemical;
    return 0;
}
