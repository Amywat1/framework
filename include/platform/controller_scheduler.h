#ifndef PLATFORM_CONTROLLER_SCHEDULER_H
#define PLATFORM_CONTROLLER_SCHEDULER_H

#include <stdbool.h>

#include "shared/result_types.h"

struct system_context_t;
typedef struct system_context_t system_context_t;
typedef struct controller_scheduler_t controller_scheduler_t;

typedef enum {
    CONTROLLER_SCHEDULER_RUNTIME_STATE_UNAVAILABLE = 0,
    CONTROLLER_SCHEDULER_RUNTIME_STATE_INITIALIZED,
    CONTROLLER_SCHEDULER_RUNTIME_STATE_RUNNING,
    CONTROLLER_SCHEDULER_RUNTIME_STATE_DRAINING,
    CONTROLLER_SCHEDULER_RUNTIME_STATE_STOPPED,
    CONTROLLER_SCHEDULER_RUNTIME_STATE_FAILED
} controller_scheduler_runtime_state_t;

typedef enum {
    CONTROLLER_SCHEDULER_EVENT_SOURCE_COMMAND = 0,
    CONTROLLER_SCHEDULER_EVENT_SOURCE_NOTIFICATION,
    CONTROLLER_SCHEDULER_EVENT_SOURCE_EXIT
} controller_scheduler_event_source_kind_t;

typedef enum {
    CONTROLLER_SCHEDULER_EVENT_SOURCE_DISABLED = 0,
    CONTROLLER_SCHEDULER_EVENT_SOURCE_ENABLED,
    CONTROLLER_SCHEDULER_EVENT_SOURCE_DEGRADED,
    CONTROLLER_SCHEDULER_EVENT_SOURCE_CLOSED
} controller_scheduler_event_source_state_t;

typedef enum {
    CONTROLLER_SCHEDULER_EXIT_MODE_DIRECT = 0,
    CONTROLLER_SCHEDULER_EXIT_MODE_BOUNDED_DRAIN
} controller_scheduler_exit_mode_t;

typedef struct controller_scheduler_config_t {
    unsigned long control_period_ms;
    bool command_event_source_enabled;
    bool notification_event_source_enabled;
    bool exit_event_source_enabled;
    controller_scheduler_exit_mode_t exit_mode;
    unsigned int bounded_drain_ticks;
    unsigned int max_triggers_per_tick;
    unsigned long overrun_warning_threshold_ms;
    bool observability_enabled;
} controller_scheduler_config_t;

typedef struct controller_scheduler_metrics_t {
    unsigned long cycle_count;
    unsigned long overrun_count;
    unsigned long consecutive_overrun_count;
    unsigned long command_event_count;
    unsigned long notification_event_count;
    unsigned long exit_event_count;
    unsigned int pending_trigger_count;
    char last_error_reason[64];
} controller_scheduler_metrics_t;

typedef struct controller_runtime_state_view_t {
    controller_scheduler_runtime_state_t runtime_state;
    unsigned long control_period_ms;
    unsigned long last_cycle_start_ms;
    unsigned long last_cycle_duration_ms;
    controller_scheduler_event_source_state_t command_source_state;
    controller_scheduler_event_source_state_t notification_source_state;
    controller_scheduler_event_source_state_t exit_source_state;
    controller_scheduler_metrics_t metrics;
} controller_runtime_state_view_t;

operation_result_t controller_scheduler_run(controller_scheduler_t *controller_scheduler);
operation_result_t controller_scheduler_request_stop(controller_scheduler_t *controller_scheduler);
operation_result_t controller_scheduler_read_view(const controller_scheduler_t *controller_scheduler,
    controller_runtime_state_view_t *controller_runtime_state_view);
operation_result_t controller_scheduler_read_context_view(const system_context_t *system_context,
    controller_runtime_state_view_t *controller_runtime_state_view);

#endif
