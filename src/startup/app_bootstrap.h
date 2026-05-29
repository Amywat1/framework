#ifndef SRC_STARTUP_APP_BOOTSTRAP_H
#define SRC_STARTUP_APP_BOOTSTRAP_H

#include <stdbool.h>
#include <stdio.h>

#include "application/coordinators/background_alarm_settings.h"
#include "domain/ports/actuator_port.h"
#include "domain/ports/sensor_port.h"
#include "platform/scheduler.h"
#include "shared/result_types.h"

/**
 * @file app_bootstrap.h
 * @brief 声明应用引导层接口（startup 模块私有）。
 *
 * @details 本模块负责将 caller 注入的资源装配为可运行应用实例。
 *          仅供 `src/startup` 内实现与入口使用，不对外发布。
 */

/**
 * @brief 应用实例创建配置。
 *
 * @note `sensor_port`、`actuator_port`、命令入站 stdio 句柄和路径参数均由调用方拥有。
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
    background_alarm_settings_t background_alarm_settings;
} app_config_t;

/**
 * @brief 将应用配置重置为默认空值。
 *
 * @param config  待初始化配置，不能为空。
 */
void app_config_init(app_config_t *config);

/**
 * @brief 创建应用单实例并完成运行时组件装配。
 *
 * @param config  创建配置，不能为空。
 * @return 创建成功时返回 `operation_result_ok()`，失败时返回显式错误结果。
 */
operation_result_t app_create(const app_config_t *config);

/**
 * @brief 运行应用单实例主循环。
 *
 * @return 正常退出返回 `operation_result_ok()`；运行期错误或状态误用返回失败结果。
 */
operation_result_t app_run(void);

/**
 * @brief 销毁应用单实例并逆序释放模块持有资源。
 *
 * @note 实例处于未创建或已销毁状态时，重复调用按安全重复销毁语义返回
 *       `operation_result_ok()`。
 * @return 销毁成功或重复销毁安全返回 `operation_result_ok()`；资源释放失败时返回显式失败结果；
 *         未初始化时调用 `app_run` 等接口返回失败结果。
 */
operation_result_t app_destroy(void);

#endif
