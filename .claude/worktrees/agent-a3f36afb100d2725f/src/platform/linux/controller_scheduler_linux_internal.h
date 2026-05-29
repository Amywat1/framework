#ifndef SRC_PLATFORM_LINUX_CONTROLLER_SCHEDULER_LINUX_INTERNAL_H
#define SRC_PLATFORM_LINUX_CONTROLLER_SCHEDULER_LINUX_INTERNAL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>

#include "platform/linux/controller_scheduler_linux.h"
#include "src/platform/linux/stdio_formal_command_adapter.h"

typedef struct scheduler_event_source_descriptor_t
{
    controller_scheduler_event_source_kind_t source_kind;
    controller_scheduler_event_source_state_t source_state;
    unsigned long trigger_count;
    unsigned long last_seen_time_ms;
} scheduler_event_source_descriptor_t;

typedef struct scheduler_notification_snapshot_t
{
    unsigned long snapshot_version;
    unsigned long captured_time_ms;
    bool dirty;
} scheduler_notification_snapshot_t;

struct controller_scheduler_t
{
    system_context_t system_context;
    controller_scheduler_config_t config;
    controller_scheduler_runtime_state_t runtime_state;
    stdio_formal_command_adapter_t command_adapter;
    int epoll_fd;
    int timer_fd;
    int wakeup_fd;
    bool exit_requested;
    bool exit_immediate;
    unsigned int drain_ticks_remaining;
    unsigned long pending_period_expirations;
    unsigned int pending_notification_count;
    bool pending_command_event;
    bool pending_exit_event;
    bool in_run_loop;
    unsigned long monotonic_epoch_ms;
    unsigned long last_cycle_start_ms;
    unsigned long last_cycle_duration_ms;
    unsigned long forced_cycle_duration_ms;
    controller_scheduler_metrics_t metrics;
    scheduler_event_source_descriptor_t command_source;
    scheduler_event_source_descriptor_t notification_source;
    scheduler_event_source_descriptor_t exit_source;
    scheduler_notification_snapshot_t notification_snapshot;
    bool failpoint_timer_read;
    bool failpoint_wakeup_read;
    bool failpoint_wakeup_write;
    bool failpoint_command_read;
    bool failpoint_main_loop_run;
};

/**
 * @brief 记录调度器错误原因，并按需切换失败态。
 * @param controller_scheduler 调度器实例。
 * @param reason_code 错误原因码。
 * @param terminal 是否同步切换到失败态。
 */
void controller_scheduler_record_error(controller_scheduler_t *controller_scheduler, const char *reason_code,
                                       bool terminal);

/**
 * @brief 刷新待处理触发数指标。
 * @param controller_scheduler 调度器实例。
 */
void controller_scheduler_update_pending_metric(controller_scheduler_t *controller_scheduler);

/**
 * @brief 记录命令事件指标和观测日志。
 * @param controller_scheduler 调度器实例。
 * @param command_line 命令行文本。
 */
void controller_scheduler_note_command_event(controller_scheduler_t *controller_scheduler, const char *command_line);

/**
 * @brief 在限定次数内执行主循环 tick，并更新周期指标。
 */
operation_result_t controller_scheduler_execute_bounded_ticks(controller_scheduler_t *controller_scheduler,
                                                             bool advance_time, unsigned long elapsed_ms,
                                                             bool count_cycle,
                                                             unsigned long cycle_count_increment);

/**
 * @brief 请求调度器进入退出流程。
 */
operation_result_t controller_scheduler_request_exit_internal(controller_scheduler_t *controller_scheduler,
                                                             bool immediate, bool signal_wakeup);

#endif
