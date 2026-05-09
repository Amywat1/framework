#ifndef SRC_PLATFORM_LINUX_CONTROLLER_SCHEDULER_LINUX_INTERNAL_H
#define SRC_PLATFORM_LINUX_CONTROLLER_SCHEDULER_LINUX_INTERNAL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>

#include "platform/linux/controller_scheduler_linux.h"

typedef struct scheduler_event_source_descriptor_t {
    controller_scheduler_event_source_kind_t source_kind;
    controller_scheduler_event_source_state_t source_state;
    unsigned long trigger_count;
    unsigned long last_seen_time_ms;
} scheduler_event_source_descriptor_t;

typedef struct scheduler_notification_snapshot_t {
    unsigned long snapshot_version;
    unsigned long captured_time_ms;
    bool dirty;
} scheduler_notification_snapshot_t;

struct controller_scheduler_t {
    system_context_t *system_context;
    controller_scheduler_config_t config;
    controller_scheduler_runtime_state_t runtime_state;
    controller_scheduler_linux_stdio_t stdio_binding;
    int epoll_fd;
    int timer_fd;
    int wakeup_fd;
    int command_fd;
    int command_fd_flags;
    bool command_fd_flags_valid;
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
    size_t command_buffer_length;
    bool command_input_eof;
    char command_buffer[512];
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

#endif
