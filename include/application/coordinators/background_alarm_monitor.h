#ifndef APPLICATION_COORDINATORS_BACKGROUND_ALARM_MONITOR_H
#define APPLICATION_COORDINATORS_BACKGROUND_ALARM_MONITOR_H

#include "application/coordinators/background_alarm_settings.h"
#include "application/coordinators/device_runtime.h"
#include "domain/ports/sensor_port.h"
#include "shared/result_types.h"

/**
 * @file background_alarm_monitor.h
 * @brief 声明后台报警监控应用层组件接口。
 */

typedef struct background_alarm_monitor_t background_alarm_monitor_t;

/**
 * @brief 后台报警监控创建配置。
 */
typedef struct background_alarm_monitor_config_t
{
    background_alarm_settings_t settings; /**< 后台报警监控通用设置。 */
    const sensor_port_t *sensor_port;     /**< 传感器读取端口。 */
} background_alarm_monitor_config_t;

/**
 * @brief 创建后台报警监控实例。
 *
 * @param monitor 输出监控实例句柄，不能为空。
 * @param config 创建配置，不能为空。
 * @return 创建成功返回 `operation_result_ok()`，失败时返回显式错误结果。
 */
operation_result_t background_alarm_monitor_create(background_alarm_monitor_t **monitor,
                                                   const background_alarm_monitor_config_t *config);

/**
 * @brief 启动后台报警监控。
 *
 * @param monitor 监控实例；允许传入 `0`，此时为空操作。
 * @return 启动成功返回 `operation_result_ok()`，失败时返回显式错误结果。
 */
operation_result_t background_alarm_monitor_start(background_alarm_monitor_t *monitor);

/**
 * @brief 停止后台报警监控并等待工作线程退出。
 *
 * @param monitor 监控实例；允许传入 `0`，此时为空操作。
 */
void background_alarm_monitor_stop(background_alarm_monitor_t *monitor);

/**
 * @brief 销毁后台报警监控实例。
 *
 * @param monitor 监控实例；允许传入 `0`。
 */
void background_alarm_monitor_destroy(background_alarm_monitor_t *monitor);

#endif
