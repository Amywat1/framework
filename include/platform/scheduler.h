#ifndef PLATFORM_SCHEDULER_H
#define PLATFORM_SCHEDULER_H

#include <stdbool.h>
#include <stdio.h>

#include "application/coordinators/scheduler_runtime_port.h"
#include "shared/result_types.h"

/**
 * @file scheduler.h
 * @brief 声明平台调度器抽象与运行态视图。
 */

typedef struct scheduler_t scheduler_t;

/**
 * @brief 调度器使用的标准输入输出绑定。
 */
typedef struct scheduler_stdio_t
{
    FILE *input;  /**< 命令输入流。 */
    FILE *output; /**< 命令响应输出流。 */
    FILE *error;  /**< 错误输出流。 */
} scheduler_stdio_t;

/**
 * @brief 调度器当前运行态。
 */
typedef enum
{
    SCHEDULER_RUNTIME_STATE_UNAVAILABLE = 0,
    SCHEDULER_RUNTIME_STATE_INITIALIZED,
    SCHEDULER_RUNTIME_STATE_RUNNING,
    SCHEDULER_RUNTIME_STATE_DRAINING,
    SCHEDULER_RUNTIME_STATE_STOPPED,
    SCHEDULER_RUNTIME_STATE_FAILED
} scheduler_runtime_state_t;

/**
 * @brief 调度器管理的外部事件源类别。
 */
typedef enum
{
    SCHEDULER_EVENT_SOURCE_COMMAND = 0,
    SCHEDULER_EVENT_SOURCE_NOTIFICATION,
    SCHEDULER_EVENT_SOURCE_EXIT
} scheduler_event_source_kind_t;

/**
 * @brief 外部事件源当前可用状态。
 */
typedef enum
{
    SCHEDULER_EVENT_SOURCE_DISABLED = 0,
    SCHEDULER_EVENT_SOURCE_ENABLED,
    SCHEDULER_EVENT_SOURCE_DEGRADED,
    SCHEDULER_EVENT_SOURCE_CLOSED
} scheduler_event_source_state_t;

/**
 * @brief 退出流程策略。
 */
typedef enum
{
    SCHEDULER_EXIT_MODE_DIRECT = 0,
    SCHEDULER_EXIT_MODE_BOUNDED_DRAIN
} scheduler_exit_mode_t;

/**
 * @brief 调度器启动配置。
 */
typedef struct scheduler_config_t
{
    unsigned long control_period_ms;
    bool command_event_source_enabled;
    bool notification_event_source_enabled;
    bool exit_event_source_enabled;
    scheduler_exit_mode_t exit_mode;
    unsigned int bounded_drain_ticks;
    unsigned int max_triggers_per_tick;
    unsigned long overrun_warning_threshold_ms;
} scheduler_config_t;

/**
 * @brief 调度器运行期间累计指标。
 */
typedef struct scheduler_metrics_t
{
    unsigned long cycle_count;
    unsigned long overrun_count;
    unsigned long consecutive_overrun_count;
    unsigned long command_event_count;
    unsigned long notification_event_count;
    unsigned long exit_event_count;
    unsigned int pending_trigger_count;
    char last_error_reason[64];
} scheduler_metrics_t;

/**
 * @brief 对外暴露的调度器运行态快照。
 */
typedef struct scheduler_state_view_t
{
    scheduler_runtime_state_t runtime_state;
    unsigned long control_period_ms;
    unsigned long last_cycle_start_ms;
    unsigned long last_cycle_duration_ms;
    scheduler_event_source_state_t command_source_state;
    scheduler_event_source_state_t notification_source_state;
    scheduler_event_source_state_t exit_source_state;
    scheduler_metrics_t metrics;
} scheduler_state_view_t;

/**
 * @brief 创建当前平台的调度器实例。
 *
 * @param runtime_port 调度器运行时端口，不能为空，各函数指针必须有效。
 * @param scheduler_config 调度器配置。
 * @param scheduler_stdio 可选的标准输入输出绑定。
 * @return 成功时返回调度器对象；参数非法或底层资源初始化失败时返回 `0`。
 */
scheduler_t *scheduler_create(const scheduler_runtime_port_t *runtime_port,
                                                    const scheduler_config_t *scheduler_config,
                                                    const scheduler_stdio_t *scheduler_stdio);

/**
 * @brief 销毁当前平台的调度器实例。
 *
 * @param scheduler 调度器对象；允许传入 `0`。
 */
void scheduler_destroy(scheduler_t *scheduler);

/**
 * @brief 启动并运行调度器主循环。
 *
 * @param scheduler 调度器对象。
 * @return 成功时返回 `operation_result_ok()`，失败时返回显式错误结果。
 */
operation_result_t scheduler_run(scheduler_t *scheduler);

/**
 * @brief 请求调度器进入停止流程。
 *
 * @param scheduler 调度器对象。
 * @return 成功时返回 `operation_result_ok()`，失败时返回显式错误结果。
 */
operation_result_t scheduler_request_stop(scheduler_t *scheduler);

/**
 * @brief 读取指定调度器的当前运行态视图。
 *
 * @param scheduler 调度器对象。
 * @param state_view 输出运行态视图。
 * @return 成功时返回 `operation_result_ok()`，失败时返回显式错误结果。
 */
operation_result_t scheduler_read_view(const scheduler_t *scheduler,
                                                  scheduler_state_view_t *state_view);

#endif
