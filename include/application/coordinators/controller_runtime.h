#ifndef APPLICATION_COORDINATORS_CONTROLLER_RUNTIME_H
#define APPLICATION_COORDINATORS_CONTROLLER_RUNTIME_H

#include <stdbool.h>
#include <stdio.h>

#include "application/coordinators/background_alarm_settings.h"
#include "application/coordinators/system_context.h"
#include "domain/ports/actuator_port.h"
#include "domain/ports/sensor_port.h"
#include "platform/controller_scheduler.h"
#include "shared/result_types.h"

/**
 * @file controller_runtime.h
 * @brief 声明主控正式 runtime/bootstrap 入口。
 */

typedef struct controller_runtime_t controller_runtime_t;

/**
 * @brief 正式 runtime 生命周期状态。
 */
typedef enum
{
    CONTROLLER_RUNTIME_STATE_UNAVAILABLE = 0,
    CONTROLLER_RUNTIME_STATE_CREATED,
    CONTROLLER_RUNTIME_STATE_RUNNING,
    CONTROLLER_RUNTIME_STATE_TERMINATED,
    CONTROLLER_RUNTIME_STATE_DESTROYED
} controller_runtime_lifecycle_state_t;

/**
 * @brief 正式 runtime 创建配置。
 *
 * @note `sensor_port`、`actuator_port`、stdio 句柄和路径参数均由调用方拥有。
 * @note runtime 只绑定这些外部资源，不负责销毁调用方对象。
 */
typedef struct controller_runtime_config_t
{
    const sensor_port_t *sensor_port;
    const actuator_port_t *actuator_port;
    const controller_scheduler_config_t *scheduler_config;
    FILE *command_input;
    FILE *command_output;
    FILE *command_error;
    const char *config_root;
    const char *event_log_path;
    background_alarm_settings_t background_alarm_monitor;
} controller_runtime_config_t;

/**
 * @brief runtime 只读生命周期视图。
 */
typedef struct controller_runtime_status_view_t
{
    controller_runtime_lifecycle_state_t lifecycle_state;
    bool system_context_acquired;
    bool scheduler_created;
    bool run_invoked;
    error_code_t last_error_code;
    char last_reason_code[64];
    bool scheduler_view_available;
    controller_runtime_state_view_t scheduler_view;
} controller_runtime_status_view_t;

/**
 * @brief 将 runtime 配置结构重置为默认空值。
 *
 * @param config 待初始化配置，不能为空。
 */
void controller_runtime_config_init(controller_runtime_config_t *config);

/**
 * @brief 校验 runtime 创建配置是否满足最小正式入口要求。
 *
 * @param config 待校验配置，不能为空。
 * @return 配置合法时返回 `operation_result_ok()`，否则返回显式失败结果。
 */
operation_result_t controller_runtime_config_validate(const controller_runtime_config_t *config);

/**
 * @brief 创建一个正式主控 runtime 实例。
 *
 * @param runtime 输出 runtime 句柄，不能为空。
 * @param config 创建配置，不能为空。
 * @return 创建成功时返回 `operation_result_ok()`，失败时返回显式错误结果。
 */
operation_result_t controller_runtime_create(controller_runtime_t **runtime, const controller_runtime_config_t *config);

/**
 * @brief 进入正式主控运行闭环。
 *
 * @param runtime 正式 runtime 句柄。
 * @return 正常退出返回 `operation_result_ok()`；运行期错误或生命周期误用返回失败结果。
 */
operation_result_t controller_runtime_run(controller_runtime_t *runtime);

/**
 * @brief 销毁正式 runtime 实例并逆序释放 runtime owned 资源。
 *
 * @note runtime 使用单实例不透明句柄；实例未被重新创建占用时，重复调用本接口按安全重复销毁语义返回
 *       `operation_result_ok()`。
 * @param runtime 正式 runtime 句柄；允许传入 `0`。
 * @return 销毁成功或重复销毁安全返回 `operation_result_ok()`；若 runtime owned
 *         资源释放失败，则返回显式失败结果；非法句柄返回失败结果。
 */
operation_result_t controller_runtime_destroy(controller_runtime_t *runtime);

/**
 * @brief 读取 runtime 当前生命周期视图。
 *
 * @note runtime 使用单实例不透明句柄；实例未被重新创建占用时，本接口仍返回
 *       `operation_result_ok()` 并输出保守的 `DESTROYED` 视图。
 * @param runtime 正式 runtime 句柄。
 * @param status_view 输出状态视图，不能为空。
 * @return 读取成功返回 `operation_result_ok()`，否则返回显式失败结果。
 */
operation_result_t controller_runtime_read_state(const controller_runtime_t *runtime,
                                                 controller_runtime_status_view_t *status_view);

#endif
