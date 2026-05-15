#include "domain/services/recovery_state_machine.h"

#include "shared/error_codes.h"
#include "shared/timeouts.h"

/**
 * @brief 设置故障原因码
 * @param failure_reason_code 指向原因码指针的指针；允许为 null
 * @param reason_code 待设置的原因码
 */
static void set_failure_reason(const char **failure_reason_code, const char *reason_code)
{
    if (failure_reason_code == 0)
    {
        return;
    }
    *failure_reason_code = reason_code;
}

/**
 * @brief 返回失败结果同时记录原因码
 * @param error_code 错误码
 * @param failure_reason_code 指向原因码指针的指针；允许为 null
 * @param reason_code 原因码
 * @return 包含错误码的失败结果
 */
static operation_result_t fail_with_reason(error_code_t error_code, const char **failure_reason_code,
                                           const char *reason_code)
{
    set_failure_reason(failure_reason_code, reason_code);
    return operation_result_fail(error_code);
}

/**
 * @brief 确认顶刷已复位到家点
 * @param sensor_port 传感器端口
 * @param[out] failure_reason_code 失败时的原因码
 * @return 顶刷已到达家点返回 ok；否则返回对应错误（超时/未到达）
 */
static operation_result_t confirm_roof_brush_home(const sensor_port_t *sensor_port, const char **failure_reason_code)
{
    runtime_snapshot_t runtime_snapshot;

    if (sensor_port == 0 || sensor_port->read_runtime_snapshot == 0)
    {
        return fail_with_reason(ERROR_CODE_INVALID_ARGUMENT, failure_reason_code, "runtime_snapshot_unavailable");
    }
    if (sensor_port->read_runtime_snapshot(sensor_port->context, &runtime_snapshot) != 0)
    {
        return fail_with_reason(ERROR_CODE_IO_FAILED, failure_reason_code, "runtime_snapshot_unavailable");
    }
    if (!runtime_snapshot.actuator_feedback.roof_brush_home_feedback_available)
    {
        return fail_with_reason(ERROR_CODE_TIMEOUT, failure_reason_code, "roof_home_feedback_timeout");
    }
    if (!runtime_snapshot.actuator_feedback.roof_brush_home_reached)
    {
        return fail_with_reason(ERROR_CODE_PRECHECK_FAILED, failure_reason_code, "roof_home_not_reached");
    }
    return operation_result_ok();
}

operation_result_t recovery_state_machine_execute(const actuator_port_t *actuator_port,
                                                  const sensor_port_t *sensor_port, recovery_mode_t mode,
                                                  const char **failure_reason_code)
{
    if (failure_reason_code != 0)
    {
        *failure_reason_code = 0;
    }
    if (actuator_port == 0 || actuator_port->stop_all == 0)
    {
        return fail_with_reason(ERROR_CODE_INVALID_ARGUMENT, failure_reason_code, "stop_all_failed");
    }
    if (actuator_port->stop_all(actuator_port->context, STOP_TIMEOUT_MS) != 0)
    {
        return fail_with_reason(ERROR_CODE_IO_FAILED, failure_reason_code, "stop_all_failed");
    }
    if (mode == RECOVERY_MODE_STOP_ONLY)
    {
        return operation_result_ok();
    }
    if (mode != RECOVERY_MODE_HOME_ROOF_BRUSH)
    {
        return fail_with_reason(ERROR_CODE_INVALID_ARGUMENT, failure_reason_code, "unsupported_recovery_mode");
    }
    if (actuator_port->home_roof_brush == 0)
    {
        return fail_with_reason(ERROR_CODE_INVALID_ARGUMENT, failure_reason_code, "roof_home_command_failed");
    }
    if (actuator_port->home_roof_brush(actuator_port->context, HOME_FEEDBACK_TIMEOUT_MS) != 0)
    {
        return fail_with_reason(ERROR_CODE_IO_FAILED, failure_reason_code, "roof_home_command_failed");
    }
    return confirm_roof_brush_home(sensor_port, failure_reason_code);
}
