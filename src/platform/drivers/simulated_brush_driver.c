#include "platform/drivers/simulated_brush_driver.h"

static int simulated_set_roof_brush_follow(void *context, bool enabled, int timeout_ms)
{
    simulated_driver_context_t *driver_context = (simulated_driver_context_t *)context;
    (void)timeout_ms;
    simulated_driver_context_lock(driver_context);
    driver_context->roof_brush_follow_enabled = enabled;
    driver_context->roof_follow_command_count += 1;
    driver_context->runtime_snapshot.actuator_feedback.roof_brush_follow_ok =
        enabled ? driver_context->roof_follow_feedback_available : true;
    driver_context->runtime_snapshot.actuator_feedback.roof_brush_stopped = !enabled;
    simulated_driver_context_unlock(driver_context);
    return 0;
}

static int simulated_set_side_brush_enabled(void *context, bool enabled, int timeout_ms)
{
    simulated_driver_context_t *driver_context = (simulated_driver_context_t *)context;
    (void)timeout_ms;
    simulated_driver_context_lock(driver_context);
    driver_context->side_brush_enabled = enabled;
    driver_context->side_brush_command_count += 1;
    driver_context->runtime_snapshot.actuator_feedback.side_brush_stopped = !enabled;
    simulated_driver_context_unlock(driver_context);
    return 0;
}

static int simulated_stop_roof_brush(void *context, int timeout_ms)
{
    simulated_driver_context_t *driver_context = (simulated_driver_context_t *)context;
    (void)timeout_ms;
    simulated_driver_context_lock(driver_context);
    if (driver_context->roof_stop_command_should_fail)
    {
        simulated_driver_context_unlock(driver_context);
        return -1;
    }
    driver_context->roof_stop_command_count += 1;
    driver_context->roof_brush_follow_enabled = false;
    driver_context->runtime_snapshot.actuator_feedback.roof_brush_follow_ok = true;
    driver_context->runtime_snapshot.actuator_feedback.roof_brush_stopped =
        driver_context->roof_stop_feedback_available;
    simulated_driver_context_unlock(driver_context);
    return 0;
}

static int simulated_stop_side_brush(void *context, int timeout_ms)
{
    simulated_driver_context_t *driver_context = (simulated_driver_context_t *)context;
    (void)timeout_ms;
    simulated_driver_context_lock(driver_context);
    if (driver_context->side_stop_command_should_fail)
    {
        simulated_driver_context_unlock(driver_context);
        return -1;
    }
    driver_context->side_stop_command_count += 1;
    driver_context->side_brush_enabled = false;
    driver_context->runtime_snapshot.actuator_feedback.side_brush_stopped =
        driver_context->side_stop_feedback_available;
    simulated_driver_context_unlock(driver_context);
    return 0;
}

static int simulated_home_roof_brush(void *context, int timeout_ms)
{
    simulated_driver_context_t *driver_context = (simulated_driver_context_t *)context;
    (void)timeout_ms;
    simulated_driver_context_lock(driver_context);
    if (driver_context->roof_home_command_should_fail)
    {
        simulated_driver_context_unlock(driver_context);
        return -1;
    }
    driver_context->roof_home_command_count += 1;
    driver_context->runtime_snapshot.actuator_feedback.roof_brush_home_reached = driver_context->roof_home_reached;
    simulated_driver_context_unlock(driver_context);
    return 0;
}

int simulated_brush_driver_bind(actuator_port_t *actuator_port, simulated_driver_context_t *driver_context)
{
    actuator_port->context = driver_context;
    actuator_port->set_roof_brush_follow = simulated_set_roof_brush_follow;
    actuator_port->set_side_brush_enabled = simulated_set_side_brush_enabled;
    actuator_port->stop_roof_brush = simulated_stop_roof_brush;
    actuator_port->stop_side_brush = simulated_stop_side_brush;
    actuator_port->home_roof_brush = simulated_home_roof_brush;
    return 0;
}
