#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/select.h>
#include <time.h>
#include <unistd.h>

#include "adapters/inbound/cli_command_adapter.h"
#include "adapters/logging/file_event_logger.h"
#include "adapters/outbound/file_program_repository.h"
#include "platform/drivers/simulated_brush_driver.h"
#include "platform/drivers/simulated_chemical_driver.h"
#include "platform/drivers/simulated_driver_context.h"
#include "platform/drivers/simulated_gantry_driver.h"
#include "platform/drivers/simulated_ro_water_driver.h"
#include "platform/drivers/simulated_sensor_driver.h"
#include "platform/drivers/simulated_dryer_driver.h"
#include "domain/services/wait_timeout_service.h"
#include "platform/linux/main_loop.h"

static unsigned long monotonic_time_ms(void)
{
    struct timespec timespec_now;

    clock_gettime(CLOCK_MONOTONIC, &timespec_now);
    return (unsigned long)(timespec_now.tv_sec * 1000ul)
        + (unsigned long)(timespec_now.tv_nsec / 1000000ul);
}

static void initialize_system_context(system_context_t *system_context, simulated_driver_context_t *driver_context)
{
    memset(system_context, 0, sizeof(*system_context));
    simulated_driver_context_init(driver_context);
    simulated_sensor_driver_bind(&system_context->sensor_port, driver_context);
    simulated_gantry_driver_bind(&system_context->actuator_port, driver_context);
    simulated_brush_driver_bind(&system_context->actuator_port, driver_context);
    simulated_chemical_driver_bind(&system_context->actuator_port, driver_context);
    simulated_ro_water_driver_bind(&system_context->actuator_port, driver_context);
    simulated_dryer_driver_bind(&system_context->actuator_port, driver_context);
    file_program_repository_init(system_context, "./configs");
    file_event_logger_init(system_context, "./runtime/logs/events.log");
}

static unsigned long compute_wait_timeout_ms(const system_context_t *system_context)
{
    if (system_context->wait_condition.active == false) {
        return 0;
    }
    if (system_context->current_time_ms >= system_context->wait_condition.timeout_at_ms) {
        return 0;
    }
    return system_context->wait_condition.timeout_at_ms - system_context->current_time_ms;
}

static int wait_for_input_or_timeout(const system_context_t *system_context)
{
    fd_set read_fds;
    struct timeval timeout_value;
    struct timeval *timeout_ptr = 0;
    unsigned long wait_timeout_ms;

    FD_ZERO(&read_fds);
    FD_SET(STDIN_FILENO, &read_fds);
    wait_timeout_ms = compute_wait_timeout_ms(system_context);
    if (system_context->wait_condition.active) {
        timeout_value.tv_sec = (long)(wait_timeout_ms / 1000ul);
        timeout_value.tv_usec = (long)((wait_timeout_ms % 1000ul) * 1000ul);
        timeout_ptr = &timeout_value;
    }

    return select(STDIN_FILENO + 1, &read_fds, 0, 0, timeout_ptr);
}

static int drain_runtime(system_context_t *system_context)
{
    operation_result_t result;

    while (system_context->pending_trigger_count > 0
        || wait_timeout_service_should_fire(&system_context->wait_condition, system_context->current_time_ms)) {
        result = main_loop_run(system_context);
        if (!result.ok) {
            fprintf(stderr, "wash_controller runtime_error=%d reason=%s\n",
                (int)result.error_code,
                system_context->last_reason_code[0] != '\0' ? system_context->last_reason_code : "none");
            return 1;
        }
    }
    return 0;
}

static int process_stdin_command(system_context_t *system_context, const char *command_line, char *response_line, size_t response_line_size)
{
    operation_result_t result;

    result = cli_command_adapter_execute_formal_line(system_context, command_line, response_line, response_line_size);
    if (!result.ok && response_line[0] == '\0') {
        fprintf(stderr, "wash_controller command_failed=%d reason=%s\n",
            (int)result.error_code,
            system_context->last_reason_code[0] != '\0' ? system_context->last_reason_code : "none");
        return 1;
    }
    printf("%s\n", response_line);
    fflush(stdout);
    return 0;
}

int main(void)
{
    char command_line[256];
    char response_line[512];
    int wait_result;
    unsigned long previous_tick_ms;
    unsigned long current_tick_ms;
    simulated_driver_context_t driver_context;
    system_context_t system_context;

    initialize_system_context(&system_context, &driver_context);
    previous_tick_ms = monotonic_time_ms();

    for (;;) {
        wait_result = wait_for_input_or_timeout(&system_context);
        current_tick_ms = monotonic_time_ms();
        main_loop_advance_time(&system_context, current_tick_ms - previous_tick_ms);
        previous_tick_ms = current_tick_ms;

        if (wait_result < 0) {
            if (errno == EINTR) {
                continue;
            }
            fprintf(stderr, "wash_controller select_failed=%d\n", errno);
            return 1;
        }

        if (wait_result > 0 && fgets(command_line, sizeof(command_line), stdin) != 0) {
            if (process_stdin_command(&system_context, command_line, response_line, sizeof(response_line)) != 0) {
                return 1;
            }
        } else if (wait_result > 0) {
            if (feof(stdin)) {
                if (drain_runtime(&system_context) != 0) {
                    return 1;
                }
                return 0;
            }
            if (ferror(stdin)) {
                fprintf(stderr, "wash_controller stdin_read_failed\n");
                return 1;
            }
            clearerr(stdin);
        }

        if (drain_runtime(&system_context) != 0) {
            return 1;
        }
    }
}
