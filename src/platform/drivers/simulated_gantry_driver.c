#include "platform/drivers/simulated_gantry_driver.h"

static int simulated_start_motion(void *context, const segment_motion_plan_t *segment_motion_plan, int timeout_ms)
{
    simulated_driver_context_t *driver_context = (simulated_driver_context_t *)context;
    (void)segment_motion_plan;
    (void)timeout_ms;
    driver_context->motion_command_count += 1;
    return 0;
}

static int simulated_stop_all(void *context, int timeout_ms)
{
    simulated_driver_context_t *driver_context = (simulated_driver_context_t *)context;
    (void)timeout_ms;
    if (driver_context->stop_all_should_fail)
    {
        return -1;
    }

    driver_context->stop_all_command_count += 1;
    driver_context->roof_brush_follow_enabled = false;
    driver_context->side_brush_enabled = false;
    driver_context->chemical_enabled = false;
    driver_context->ro_water_enabled = false;
    driver_context->dryer_enabled = false;
    driver_context->runtime_snapshot.actuator_feedback.roof_brush_stopped = true;
    driver_context->runtime_snapshot.actuator_feedback.side_brush_stopped = true;
    driver_context->runtime_snapshot.actuator_feedback.chemical_closed = true;
    driver_context->runtime_snapshot.actuator_feedback.ro_water_closed = true;
    driver_context->runtime_snapshot.actuator_feedback.dryer_closed = true;
    return 0;
}

int simulated_gantry_driver_bind(actuator_port_t *actuator_port, simulated_driver_context_t *driver_context)
{
    actuator_port->context = driver_context;
    actuator_port->start_motion = simulated_start_motion;
    actuator_port->stop_all = simulated_stop_all;
    return 0;
}
