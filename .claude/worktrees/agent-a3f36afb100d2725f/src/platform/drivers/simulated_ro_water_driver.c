#include "platform/drivers/simulated_ro_water_driver.h"

static int simulated_set_ro_water_enabled(void *context, bool enabled, int timeout_ms)
{
    simulated_driver_context_t *driver_context = (simulated_driver_context_t *)context;
    (void)timeout_ms;
    simulated_driver_context_lock(driver_context);
    driver_context->ro_water_enabled = enabled;
    driver_context->ro_water_command_count += 1;
    driver_context->runtime_snapshot.actuator_feedback.ro_water_closed = !enabled;
    simulated_driver_context_unlock(driver_context);
    return 0;
}

static int simulated_close_ro_water(void *context, int timeout_ms)
{
    simulated_driver_context_t *driver_context = (simulated_driver_context_t *)context;
    (void)timeout_ms;
    simulated_driver_context_lock(driver_context);
    if (driver_context->ro_water_close_command_should_fail)
    {
        simulated_driver_context_unlock(driver_context);
        return -1;
    }
    driver_context->ro_water_close_command_count += 1;
    driver_context->ro_water_enabled = false;
    driver_context->runtime_snapshot.actuator_feedback.ro_water_closed =
        driver_context->ro_water_close_feedback_available;
    simulated_driver_context_unlock(driver_context);
    return 0;
}

int simulated_ro_water_driver_bind(actuator_port_t *actuator_port, simulated_driver_context_t *driver_context)
{
    actuator_port->context = driver_context;
    actuator_port->set_ro_water_enabled = simulated_set_ro_water_enabled;
    actuator_port->close_ro_water = simulated_close_ro_water;
    return 0;
}
