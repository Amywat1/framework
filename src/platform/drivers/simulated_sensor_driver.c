#include "platform/drivers/simulated_sensor_driver.h"

static void sync_feedback_capabilities(simulated_driver_context_t *driver_context)
{
    if (driver_context == 0)
    {
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

static int simulated_read_snapshot(void *context, sensor_snapshot_t *sensor_snapshot)
{
    const simulated_driver_context_t *driver_context = (const simulated_driver_context_t *)context;
    simulated_driver_context_t *mutable_driver_context;

    if (driver_context == 0 || sensor_snapshot == 0)
    {
        return -1;
    }
    mutable_driver_context = (simulated_driver_context_t *)driver_context;
    simulated_driver_context_lock(mutable_driver_context);
    if (driver_context->precheck_read_should_fail)
    {
        simulated_driver_context_unlock(mutable_driver_context);
        return -1;
    }
    sensor_snapshot->safety_ok = !driver_context->precheck_safety_should_fail;
    sensor_snapshot->estop_active = driver_context->precheck_estop_active;
    sensor_snapshot->vehicle_present = !driver_context->precheck_vehicle_absent;
    sensor_snapshot->vehicle_allowed = !driver_context->precheck_vehicle_not_allowed;
    sensor_snapshot->resource_ok = !driver_context->precheck_resource_fail;
    sensor_snapshot->position_ok =
        !driver_context->precheck_position_fail && driver_context->runtime_snapshot.position_snapshot.position_valid;
    simulated_driver_context_unlock(mutable_driver_context);
    return 0;
}

static int simulated_read_runtime_snapshot(void *context, runtime_snapshot_t *runtime_snapshot)
{
    simulated_driver_context_t *driver_context = (simulated_driver_context_t *)context;

    if (driver_context == 0 || runtime_snapshot == 0)
    {
        return -1;
    }
    simulated_driver_context_lock(driver_context);
    if (driver_context->runtime_snapshot_read_should_fail)
    {
        simulated_driver_context_unlock(driver_context);
        return -1;
    }
    sync_feedback_capabilities(driver_context);
    *runtime_snapshot = driver_context->runtime_snapshot;
    simulated_driver_context_unlock(driver_context);
    return 0;
}

int simulated_sensor_driver_bind(sensor_port_t *sensor_port, simulated_driver_context_t *driver_context)
{
    sensor_port->context = driver_context;
    sensor_port->read_runtime_snapshot = simulated_read_runtime_snapshot;
    sensor_port->read_snapshot = simulated_read_snapshot;
    return 0;
}
