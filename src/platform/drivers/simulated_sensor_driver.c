#include "platform/drivers/simulated_sensor_driver.h"

#include <string.h>

static void sync_feedback_capabilities(simulated_driver_context_t *driver_context)
{
    if (driver_context == 0) {
        return;
    }

    driver_context->runtime_snapshot.actuator_feedback.roof_brush_stopped_feedback_available =
        driver_context->roof_stop_feedback_available;
    driver_context->runtime_snapshot.actuator_feedback.side_brush_stopped_feedback_available =
        driver_context->side_stop_feedback_available;
    driver_context->runtime_snapshot.actuator_feedback.chemical_closed_feedback_available =
        driver_context->chemical_close_feedback_available;
    driver_context->runtime_snapshot.actuator_feedback.ro_water_closed_feedback_available =
        driver_context->ro_water_close_feedback_available;
    driver_context->runtime_snapshot.actuator_feedback.dryer_closed_feedback_available =
        driver_context->dryer_close_feedback_available;
    driver_context->runtime_snapshot.actuator_feedback.roof_brush_home_feedback_available =
        driver_context->roof_home_feedback_available;
}

void simulated_driver_context_init(simulated_driver_context_t *driver_context)
{
    if (driver_context == 0) {
        return;
    }

    memset(driver_context, 0, sizeof(*driver_context));
    driver_context->runtime_snapshot.position_snapshot.position_valid = true;
    driver_context->roof_follow_feedback_available = true;
    driver_context->roof_stop_feedback_available = true;
    driver_context->side_stop_feedback_available = true;
    driver_context->chemical_close_feedback_available = true;
    driver_context->ro_water_close_feedback_available = true;
    driver_context->dryer_close_feedback_available = true;
    driver_context->roof_home_feedback_available = true;
    driver_context->runtime_snapshot.actuator_feedback.roof_brush_follow_ok = true;
    driver_context->runtime_snapshot.actuator_feedback.roof_brush_stopped = true;
    driver_context->runtime_snapshot.actuator_feedback.side_brush_stopped = true;
    driver_context->runtime_snapshot.actuator_feedback.chemical_closed = true;
    driver_context->runtime_snapshot.actuator_feedback.ro_water_closed = true;
    driver_context->runtime_snapshot.actuator_feedback.dryer_closed = true;
    driver_context->runtime_snapshot.actuator_feedback.roof_brush_home_reached = true;
    sync_feedback_capabilities(driver_context);
}

static int simulated_read_runtime_snapshot(void *context, runtime_snapshot_t *runtime_snapshot)
{
    simulated_driver_context_t *driver_context = (simulated_driver_context_t *)context;

    if (driver_context == 0 || runtime_snapshot == 0) {
        return -1;
    }
    if (driver_context->runtime_snapshot_read_should_fail) {
        return -1;
    }
    sync_feedback_capabilities(driver_context);
    *runtime_snapshot = driver_context->runtime_snapshot;
    return 0;
}

int simulated_sensor_driver_bind(sensor_port_t *sensor_port, simulated_driver_context_t *driver_context)
{
    sensor_port->context = driver_context;
    sensor_port->read_runtime_snapshot = simulated_read_runtime_snapshot;
    return 0;
}
