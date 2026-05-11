#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <string.h>

#include "adapters/logging/file_event_logger.h"
#include "adapters/outbound/file_program_repository.h"
#include "platform/drivers/simulated_brush_driver.h"
#include "platform/drivers/simulated_chemical_driver.h"
#include "platform/drivers/simulated_driver_context.h"
#include "platform/drivers/simulated_dryer_driver.h"
#include "platform/drivers/simulated_gantry_driver.h"
#include "platform/drivers/simulated_ro_water_driver.h"
#include "platform/drivers/simulated_sensor_driver.h"
#include "platform/linux/controller_scheduler_linux.h"

#define CONTROLLER_CONTROL_PERIOD_MS 100ul
#define CONTROLLER_BOUNDED_DRAIN_TICKS 8u
#define CONTROLLER_MAX_TRIGGERS_PER_TICK 1u

static void initialize_system_context(system_context_t system_context, simulated_driver_context_t *driver_context)
{
    sensor_port_t sensor_port;
    actuator_port_t actuator_port;

    simulated_driver_context_init(driver_context);
    memset(&sensor_port, 0, sizeof(sensor_port));
    memset(&actuator_port, 0, sizeof(actuator_port));
    simulated_sensor_driver_bind(&sensor_port, driver_context);
    simulated_gantry_driver_bind(&actuator_port, driver_context);
    simulated_brush_driver_bind(&actuator_port, driver_context);
    simulated_chemical_driver_bind(&actuator_port, driver_context);
    simulated_ro_water_driver_bind(&actuator_port, driver_context);
    simulated_dryer_driver_bind(&actuator_port, driver_context);
    system_context_set_sensor_port(system_context, &sensor_port);
    system_context_set_actuator_port(system_context, &actuator_port);
    file_program_repository_init(system_context, "./configs");
    file_event_logger_init(system_context, "./runtime/logs/events.log");
}

static void initialize_scheduler_config(controller_scheduler_config_t *controller_scheduler_config)
{
    memset(controller_scheduler_config, 0, sizeof(*controller_scheduler_config));
    controller_scheduler_config->control_period_ms = CONTROLLER_CONTROL_PERIOD_MS;
    controller_scheduler_config->command_event_source_enabled = true;
    controller_scheduler_config->notification_event_source_enabled = false;
    controller_scheduler_config->exit_event_source_enabled = true;
    controller_scheduler_config->exit_mode = CONTROLLER_SCHEDULER_EXIT_MODE_BOUNDED_DRAIN;
    controller_scheduler_config->bounded_drain_ticks = CONTROLLER_BOUNDED_DRAIN_TICKS;
    controller_scheduler_config->max_triggers_per_tick = CONTROLLER_MAX_TRIGGERS_PER_TICK;
    controller_scheduler_config->overrun_warning_threshold_ms = CONTROLLER_CONTROL_PERIOD_MS;
    controller_scheduler_config->observability_enabled = true;
}

int main(void)
{
    controller_runtime_state_view_t controller_runtime_state_view;
    controller_scheduler_config_t controller_scheduler_config;
    controller_scheduler_linux_stdio_t controller_scheduler_linux_stdio;
    simulated_driver_context_t driver_context;
    controller_scheduler_t *controller_scheduler;
    system_context_t system_context;
    operation_result_t result;
    int exit_code;

    result = system_context_acquire(&system_context);
    if (!result.ok || system_context == 0) {
        fprintf(stderr, "wash_controller context_acquire_failed\n");
        return 1;
    }

    initialize_system_context(system_context, &driver_context);
    initialize_scheduler_config(&controller_scheduler_config);
    memset(&controller_scheduler_linux_stdio, 0, sizeof(controller_scheduler_linux_stdio));
    controller_scheduler_linux_stdio.input = stdin;
    controller_scheduler_linux_stdio.output = stdout;
    controller_scheduler_linux_stdio.error = stderr;

    controller_scheduler = controller_scheduler_linux_create(system_context,
        &controller_scheduler_config,
        &controller_scheduler_linux_stdio);
    if (controller_scheduler == 0) {
        fprintf(stderr, "wash_controller scheduler_create_failed\n");
        (void)system_context_release(system_context);
        return 1;
    }

    result = controller_scheduler_run(controller_scheduler);
    exit_code = 0;
    if (!result.ok) {
        controller_scheduler_read_view(controller_scheduler, &controller_runtime_state_view);
        fprintf(stderr, "wash_controller scheduler_failed reason=%s domain_reason=%s\n",
            controller_runtime_state_view.metrics.last_error_reason[0] != '\0'
                ? controller_runtime_state_view.metrics.last_error_reason
                : "none",
            system_context_last_reason_code(system_context)[0] != '\0'
                ? system_context_last_reason_code(system_context)
                : "none");
        exit_code = 1;
    }

    controller_scheduler_linux_destroy(controller_scheduler);
    (void)system_context_release(system_context);
    return exit_code;
}
