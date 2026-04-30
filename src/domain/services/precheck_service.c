#include "domain/services/precheck_service.h"

#include "shared/error_codes.h"

operation_result_t precheck_service_run(const sensor_port_t *sensor_port)
{
    sensor_snapshot_t snapshot;

    if (sensor_port == 0 || sensor_port->read_snapshot == 0) {
        return operation_result_fail(ERROR_CODE_INVALID_ARGUMENT);
    }
    if (sensor_port->read_snapshot(sensor_port->context, &snapshot) != 0) {
        return operation_result_fail(ERROR_CODE_IO_FAILED);
    }
    if (!snapshot.safety_ok || snapshot.estop_active) {
        return operation_result_fail(ERROR_CODE_SAFETY_INTERLOCK_ACTIVE);
    }
    if (!snapshot.vehicle_present || !snapshot.vehicle_allowed) {
        return operation_result_fail(ERROR_CODE_VEHICLE_NOT_ALLOWED);
    }
    if (!snapshot.resource_ok || !snapshot.position_ok) {
        return operation_result_fail(ERROR_CODE_PRECHECK_FAILED);
    }
    return operation_result_ok();
}

