#include "domain/services/precheck_service.h"

#include "shared/error_codes.h"

operation_result_t precheck_service_run(const sensor_port_t *sensor_port)
{
    sensor_snapshot_t snapshot;

    if (sensor_port == 0 || sensor_port->read_snapshot == 0)
    {
        return operation_result_fail(ERROR_CODE_INVALID_ARGUMENT);
    }
    /* 读取启动前快照：安全互锁、急停、车型、资源、位置 */
    if (sensor_port->read_snapshot(sensor_port->context, &snapshot) != 0)
    {
        return operation_result_fail(ERROR_CODE_IO_FAILED);
    }
    /* 安全互锁检查（最高优先级）：禁止在安全互锁激活或急停信号有效时启动 */
    if (!snapshot.safety_ok || snapshot.estop_active)
    {
        return operation_result_fail(ERROR_CODE_SAFETY_INTERLOCK_ACTIVE);
    }
    /* 车型检查：车辆必须存在且符合当前程序允许的车型范围 */
    if (!snapshot.vehicle_present || !snapshot.vehicle_allowed)
    {
        return operation_result_fail(ERROR_CODE_VEHICLE_NOT_ALLOWED);
    }
    /* 资源与位置检查：水、电、气等资源就绪；龙门位置在安全起始区 */
    if (!snapshot.resource_ok || !snapshot.position_ok)
    {
        return operation_result_fail(ERROR_CODE_PRECHECK_FAILED);
    }
    return operation_result_ok();
}
