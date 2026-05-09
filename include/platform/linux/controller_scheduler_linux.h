#ifndef PLATFORM_LINUX_CONTROLLER_SCHEDULER_LINUX_H
#define PLATFORM_LINUX_CONTROLLER_SCHEDULER_LINUX_H

#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>

#include "application/coordinators/system_context.h"
#include "platform/controller_scheduler.h"

typedef struct controller_scheduler_linux_stdio_t {
    FILE *input;
    FILE *output;
    FILE *error;
} controller_scheduler_linux_stdio_t;

typedef enum {
    CONTROLLER_SCHEDULER_LINUX_TEST_FAIL_NONE = 0,
    CONTROLLER_SCHEDULER_LINUX_TEST_FAIL_TIMER_READ,
    CONTROLLER_SCHEDULER_LINUX_TEST_FAIL_WAKEUP_READ,
    CONTROLLER_SCHEDULER_LINUX_TEST_FAIL_WAKEUP_WRITE,
    CONTROLLER_SCHEDULER_LINUX_TEST_FAIL_COMMAND_READ,
    CONTROLLER_SCHEDULER_LINUX_TEST_FAIL_MAIN_LOOP_RUN
} controller_scheduler_linux_test_failpoint_t;

controller_scheduler_t *controller_scheduler_linux_create(system_context_t *system_context,
    const controller_scheduler_config_t *controller_scheduler_config,
    const controller_scheduler_linux_stdio_t *controller_scheduler_linux_stdio);
void controller_scheduler_linux_destroy(controller_scheduler_t *controller_scheduler);

operation_result_t controller_scheduler_linux_test_inject_period(controller_scheduler_t *controller_scheduler,
    unsigned int expiration_count);
operation_result_t controller_scheduler_linux_test_inject_command(controller_scheduler_t *controller_scheduler,
    const char *command_line,
    char *response_line,
    size_t response_line_size);
operation_result_t controller_scheduler_linux_test_inject_notification(controller_scheduler_t *controller_scheduler,
    unsigned int notification_count);
operation_result_t controller_scheduler_linux_test_inject_exit(controller_scheduler_t *controller_scheduler,
    bool immediate);
operation_result_t controller_scheduler_linux_test_step(controller_scheduler_t *controller_scheduler);
operation_result_t controller_scheduler_linux_test_poll_once(controller_scheduler_t *controller_scheduler);
void controller_scheduler_linux_test_set_failpoint(controller_scheduler_t *controller_scheduler,
    controller_scheduler_linux_test_failpoint_t controller_scheduler_linux_test_failpoint,
    bool enabled);
void controller_scheduler_linux_test_set_cycle_duration(controller_scheduler_t *controller_scheduler,
    unsigned long cycle_duration_ms);

#endif
