#ifndef DOMAIN_SERVICES_RECOVERY_STATE_MACHINE_H
#define DOMAIN_SERVICES_RECOVERY_STATE_MACHINE_H

#include "domain/ports/actuator_port.h"
#include "domain/ports/sensor_port.h"
#include "shared/result_types.h"

/**
 * @file recovery_state_machine.h
 * @brief 定义 recovery/homing 的最小闭环流程。
 */
/**
 * @brief 描述恢复状态机支持的执行模式。
 */
typedef enum recovery_mode_t
{
    /**< 仅执行安全停止恢复。 */
    RECOVERY_MODE_STOP_ONLY = 0,
    /**< 执行停机后再回零顶刷。 */
    RECOVERY_MODE_HOME_ROOF_BRUSH
} recovery_mode_t;

/**
 * @brief 执行 recovery/homing 流程。
 *
 * @param actuator_port 执行器端口，不能为空。
 * @param sensor_port 传感器端口；当 `mode` 为 `RECOVERY_MODE_STOP_ONLY` 时可传 `0`。
 * @param mode recovery 模式。
 * @param failure_reason_code 失败原因输出；传入 `0` 时忽略。
 * @return `ok=true` 表示流程完成，否则返回错误码。
 */
operation_result_t recovery_state_machine_execute(const actuator_port_t *actuator_port,
                                                  const sensor_port_t *sensor_port, recovery_mode_t mode,
                                                  const char **failure_reason_code);

#endif
