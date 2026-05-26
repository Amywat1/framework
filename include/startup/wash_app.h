#ifndef STARTUP_WASH_APP_H
#define STARTUP_WASH_APP_H

#include <stdbool.h>
#include <stdio.h>

#include "application/coordinators/background_alarm_settings.h"
#include "application/coordinators/device_runtime.h"
#include "domain/ports/actuator_port.h"
#include "domain/ports/sensor_port.h"
#include "platform/controller_scheduler.h"
#include "shared/result_types.h"

/**
 * @file wash_app.h
 * @brief 声明主控正式 启动与生命周期管理接口。
 */

typedef struct wash_app_t wash_app_t;

/**
 * @brief wash_app 生命周期状态枚举。
 */
typedef enum
{
    WASH_APP_STATE_UNAVAILABLE = 0,
    WASH_APP_STATE_CREATED,
    WASH_APP_STATE_RUNNING,
    WASH_APP_STATE_TERMINATED,
    WASH_APP_STATE_DESTROYED
} wash_app_lifecycle_state_t;

/**
 * @brief wash_app 创建配置。
 *
 * @note `sensor_port`、`actuator_port`、stdio 句柄和路径参数均由调用方拥有。
 * @note wash_app 只绑定这些外部资源，不负责销毁调用方对象。
 */
typedef struct wash_app_config_t
{
    const sensor_port_t *sensor_port;
    const actuator_port_t *actuator_port;
    const controller_scheduler_config_t *scheduler_config;
    FILE *command_input;
    FILE *command_output;
    FILE *command_error;
    const char *config_root;
    background_alarm_settings_t background_alarm_monitor;
} wash_app_config_t;

/**
 * @brief wash_app 只读状态视图。
 */
typedef struct wash_app_status_view_t
{
    wash_app_lifecycle_state_t lifecycle_state;
    bool device_runtime_acquired;
    bool scheduler_created;
    bool scheduler_view_available;
    controller_scheduler_state_view_t scheduler_view;
} wash_app_status_view_t;

/**
 * @brief 将 wash_app 配置结构重置为默认空值。
 *
 * @param config 待初始化配置，不能为空。
 */
void wash_app_config_init(wash_app_config_t *config);

/**
 * @brief 校验 wash_app 创建配置是否满足最小要求。
 *
 * @param config 待校验配置，不能为空。
 * @return 配置合法时返回 `operation_result_ok()`，否则返回显式失败结果。
 */
operation_result_t wash_app_config_validate(const wash_app_config_t *config);

/**
 * @brief 创建 wash_app 实例。
 *
 * @param runtime 输出 runtime 句柄，不能为空。
 * @param config 创建配置，不能为空。
 * @return 创建成功时返回 `operation_result_ok()`，失败时返回显式错误结果。
 */
operation_result_t wash_app_create(wash_app_t **runtime, const wash_app_config_t *config);

/**
 * @brief 进入正式主控运行闭环。
 *
 * @param runtime wash_app 句柄。
 * @return 正常退出返回 `operation_result_ok()`；运行期错误或生命周期误用返回失败结果。
 */
operation_result_t wash_app_run(wash_app_t *runtime);

/**
 * @brief 销毁 wash_app 实例并逆序释放所有资源。
 *
 * @note wash_app 使用单实例不透明句柄；实例未被重新创建占用时，重复调用本接口按安全重复销毁语义返回
 *       `operation_result_ok()`。
 * @param runtime wash_app 句柄；允许传入 `0`。
 * @return 销毁成功或重复销毁安全返回 `operation_result_ok()`；若 runtime owned
 *         资源释放失败，则返回显式失败结果；非法句柄返回失败结果。
 */
operation_result_t wash_app_destroy(wash_app_t *runtime);

/**
 * @brief 读取 wash_app 当前生命周期视图。
 *
 * @note wash_app 使用单实例不透明句柄；实例未被重新创建占用时，本接口仍返回
 *       `operation_result_ok()` 并输出保守的 `DESTROYED` 视图。
 * @param runtime wash_app 句柄。
 * @param status_view 输出状态视图，不能为空。
 * @return 读取成功返回 `operation_result_ok()`，否则返回显式失败结果。
 */
operation_result_t wash_app_read_state(const wash_app_t *runtime,
                                                 wash_app_status_view_t *status_view);

#endif
