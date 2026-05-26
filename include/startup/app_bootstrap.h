#ifndef STARTUP_APP_BOOTSTRAP_H
#define STARTUP_APP_BOOTSTRAP_H

#include <stdbool.h>
#include <stdio.h>

#include "application/coordinators/background_alarm_settings.h"
#include "application/coordinators/device_runtime.h"
#include "domain/ports/actuator_port.h"
#include "domain/ports/sensor_port.h"
#include "platform/scheduler.h"
#include "shared/result_types.h"

/**
 * @file app_bootstrap.h
 * @brief 声明应用引导层接口。
 *
 * @details 本模块负责将 caller 注入的资源装配为可运行应用实例。
 *          公开 API 使用 create/run/destroy 表达实例生命周期，文件名体现模块归属。
 */

typedef struct app_t app_t;

/**
 * @brief 应用实例生命周期状态。
 */
typedef enum app_state_t
{
    APP_STATE_UNAVAILABLE = 0,
    APP_STATE_CREATED,
    APP_STATE_RUNNING,
    APP_STATE_TERMINATED,
    APP_STATE_DESTROYED
} app_state_t;

/**
 * @brief 应用实例创建配置。
 *
 * @note `sensor_port`、`actuator_port`、stdio 句柄和路径参数均由调用方拥有。
 * @note 引导层只绑定这些外部资源，不负责销毁调用方对象。
 */
typedef struct app_config_t
{
    const sensor_port_t *sensor_port;
    const actuator_port_t *actuator_port;
    const scheduler_config_t *scheduler_config;
    FILE *command_input;
    FILE *command_output;
    FILE *command_error;
    const char *config_root;
    background_alarm_settings_t background_alarm_monitor;
} app_config_t;

/**
 * @brief 应用实例只读状态视图。
 */
typedef struct app_status_view_t
{
    app_state_t state;
    bool device_runtime_acquired;
    bool scheduler_created;
    bool scheduler_view_available;
    bool domain_reason_available;
    char domain_last_reason_code[64];
    scheduler_state_view_t scheduler_view;
} app_status_view_t;

/**
 * @brief 将应用配置重置为默认空值。
 *
 * @param config  待初始化配置，不能为空。
 */
void app_config_init(app_config_t *config);

/**
 * @brief 校验应用创建配置是否满足最小要求。
 *
 * @param config  待校验配置，不能为空。
 * @return 配置合法时返回 `operation_result_ok()`，否则返回显式失败结果。
 */
operation_result_t app_config_validate(const app_config_t *config);

/**
 * @brief 创建应用实例并完成运行时组件装配。
 *
 * @param out_app  输出应用句柄，不能为空。
 * @param config   创建配置，不能为空。
 * @return 创建成功时返回 `operation_result_ok()`，失败时返回显式错误结果。
 */
operation_result_t app_create(app_t **out_app, const app_config_t *config);

/**
 * @brief 运行应用实例主循环。
 *
 * @param app  应用句柄，不能为空。
 * @return 正常退出返回 `operation_result_ok()`；运行期错误或状态误用返回失败结果。
 */
operation_result_t app_run(app_t *app);

/**
 * @brief 销毁应用实例并逆序释放模块持有资源。
 *
 * @note 使用单实例不透明句柄；实例未被重新占用时，重复调用按安全重复销毁语义返回
 *       `operation_result_ok()`。
 * @param app  应用句柄；允许传入 `0`。
 * @return 销毁成功或重复销毁安全返回 `operation_result_ok()`；资源释放失败时返回显式失败结果；
 *         非法句柄返回失败结果。
 */
operation_result_t app_destroy(app_t *app);

/**
 * @brief 读取应用实例当前状态视图。
 *
 * @note 使用单实例不透明句柄；实例未被重新占用时，仍返回
 *       `operation_result_ok()` 并输出保守的 `APP_STATE_DESTROYED` 视图。
 * @param app          应用句柄，不能为空。
 * @param status_view  输出状态视图，不能为空。
 * @return 读取成功返回 `operation_result_ok()`，否则返回显式失败结果。
 */
operation_result_t app_read_state(const app_t *app, app_status_view_t *status_view);

#endif
