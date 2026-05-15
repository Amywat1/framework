#include "platform/drivers/simulated_dryer_driver.h"

static int simulated_set_dryer_enabled(void *context, bool enabled, int timeout_ms)
{
    simulated_driver_context_t *driver_context = (simulated_driver_context_t *)context;
    (void)timeout_ms;
    driver_context->dryer_enabled = enabled;
    driver_context->dryer_command_count += 1;
    driver_context->runtime_snapshot.actuator_feedback.dryer_closed = !enabled;
    return 0;
}

static int simulated_close_dryer(void *context, int timeout_ms)
{
    simulated_driver_context_t *driver_context = (simulated_driver_context_t *)context;
    (void)timeout_ms;
    if (driver_context->dryer_close_command_should_fail)
    {
        return -1;
    }
    driver_context->dryer_close_command_count += 1;
    driver_context->dryer_enabled = false;
    driver_context->runtime_snapshot.actuator_feedback.dryer_closed = driver_context->dryer_close_feedback_available;
    return 0;
}

int simulated_dryer_driver_bind(actuator_port_t *actuator_port, simulated_driver_context_t *driver_context)
{
    actuator_port->context = driver_context;
    actuator_port->set_dryer_enabled = simulated_set_dryer_enabled;
    actuator_port->close_dryer = simulated_close_dryer;
    return 0;
}
