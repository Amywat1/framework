#ifndef PLATFORM_SCHEDULER_H
#define PLATFORM_SCHEDULER_H

#include <stdbool.h>
#include <stdio.h>

#include "application/coordinators/scheduler_runtime_port.h"
#include "application/ports/inbound/command_port.h"
#include "application/ports/outbound/scheduler_sync_port.h"
#include "shared/result_types.h"

/**
 * @file scheduler.h
 * @brief 声明平台调度器抽象与运行态视图。
 */

typedef struct scheduler_t scheduler_t;

/**
 * @brief 命令输入源端口：调度器通过此端口监听并分发命令行事件。
 * @details 调用方负责在传入前完成 IO 使能（如设置非阻塞标志）。
 *          调度器销毁时调用 restore 恢复 IO 状态。
 */
typedef struct command_source_port_t
{
    int fd;                  /**< 注册到 epoll 的命令输入 fd；-1 表示无可用输入源。 */
    /** @brief fd 可读时调用，读取数据并通过 command_port 分发命令。 */
    operation_result_t (*on_readable)(void *context, const command_port_t *command_port,
                                       bool *command_processed);
    /** @brief 判断缓冲区是否还有待处理的完整命令行。 */
    bool (*has_buffered_data)(void *context);
    /** @brief 判断 EOF 已到达且缓冲区已耗尽。 */
    bool (*is_eof_and_drained)(void *context);
    /** @brief 恢复 IO 状态（如撤销非阻塞标志）。 */
    void (*restore)(void *context);
    void *context;           /**< 实现侧上下文指针。 */
} command_source_port_t;

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
                                                    const command_source_port_t *command_source_port);

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

/**
 * @brief 构建指向指定调度器实例的同步执行出站端口。
 *
 * @param scheduler 调度器对象，不能为空。
 * @return 填充完毕的同步执行端口；scheduler 为空时返回函数指针全为 `0` 的空端口。
 */
scheduler_sync_port_t scheduler_build_sync_port(scheduler_t *scheduler);

/**
 * @brief 设置调度器持有的命令处理入站端口。
 *
 * @param scheduler 调度器对象，不能为空。
 * @param command_port 命令处理端口，不能为空。
 */
void scheduler_set_command_port(scheduler_t *scheduler, const command_port_t *command_port);

#endif
