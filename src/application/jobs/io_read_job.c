#include "src/application/jobs/io_read_job.h"

operation_result_t io_read_job_read_snapshot(const sensor_port_t *sensor_port, sensor_snapshot_t *sensor_snapshot)
{
    if (sensor_port == 0 || sensor_snapshot == 0 || sensor_port->read_snapshot == 0)
    {
        return operation_result_fail(ERROR_CODE_INVALID_ARGUMENT);
    }

    if (sensor_port->read_snapshot(sensor_port->context, sensor_snapshot) != 0)
    {
        return operation_result_fail(ERROR_CODE_IO_FAILED);
    }

    return operation_result_ok();
}
