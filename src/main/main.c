#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <string.h>

#include "application/coordinators/controller_runtime.h"
#include "platform/drivers/simulated_brush_driver.h"
#include "platform/drivers/simulated_chemical_driver.h"
#include "platform/drivers/simulated_driver_context.h"
#include "platform/drivers/simulated_dryer_driver.h"
#include "platform/drivers/simulated_gantry_driver.h"
#include "platform/drivers/simulated_ro_water_driver.h"
#include "platform/drivers/simulated_sensor_driver.h"

#define CONTROLLER_CONTROL_PERIOD_MS 100ul
#define CONTROLLER_BOUNDED_DRAIN_TICKS 8u
#define CONTROLLER_MAX_TRIGGERS_PER_TICK 1u

static void initialize_ports(simulated_driver_context_t *driver_context,
    sensor_port_t *sensor_port,
    actuator_port_t *actuator_port)
{
    simulated_driver_context_init(driver_context);
    memset(sensor_port, 0, sizeof(*sensor_port));
    memset(actuator_port, 0, sizeof(*actuator_port));
    simulated_sensor_driver_bind(sensor_port, driver_context);
    simulated_gantry_driver_bind(actuator_port, driver_context);
    simulated_brush_driver_bind(actuator_port, driver_context);
    simulated_chemical_driver_bind(actuator_port, driver_context);
    simulated_ro_water_driver_bind(actuator_port, driver_context);
    simulated_dryer_driver_bind(actuator_port, driver_context);
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
    controller_runtime_status_view_t runtime_status_view;
    controller_runtime_config_t controller_runtime_config;
    controller_scheduler_config_t controller_scheduler_config;
    simulated_driver_context_t driver_context;
    controller_runtime_t *controller_runtime;
    sensor_port_t sensor_port;
    actuator_port_t actuator_port;
    operation_result_t result;
    int exit_code;

    initialize_ports(&driver_context, &sensor_port, &actuator_port);
    initialize_scheduler_config(&controller_scheduler_config);
    controller_runtime_config_init(&controller_runtime_config);
    controller_runtime_config.sensor_port = &sensor_port;
    controller_runtime_config.actuator_port = &actuator_port;
    controller_runtime_config.scheduler_config = &controller_scheduler_config;
    controller_runtime_config.command_input = stdin;
    controller_runtime_config.command_output = stdout;
    controller_runtime_config.command_error = stderr;
    controller_runtime_config.config_root = "./configs";
    controller_runtime_config.event_log_path = "./runtime/logs/events.log";

    result = controller_runtime_create(&controller_runtime, &controller_runtime_config);
    if (!result.ok || controller_runtime == 0) {
        fprintf(stderr, "wash_controller runtime_create_failed error_code=%d\n", (int)result.error_code);
        return 1;
    }

    result = controller_runtime_run(controller_runtime);
    exit_code = 0;
    if (!result.ok) {
        (void)controller_runtime_read_state(controller_runtime, &runtime_status_view);
        fprintf(stderr, "wash_controller scheduler_failed reason=%s domain_reason=%s\n",
            runtime_status_view.scheduler_view_available
                    && runtime_status_view.scheduler_view.metrics.last_error_reason[0] != '\0'
                ? runtime_status_view.scheduler_view.metrics.last_error_reason
                : "none",
            runtime_status_view.last_reason_code[0] != '\0'
                ? runtime_status_view.last_reason_code
                : "none");
        exit_code = 1;
    }

    result = controller_runtime_destroy(controller_runtime);
    if (!result.ok) {
        fprintf(stderr, "wash_controller runtime_destroy_failed error_code=%d\n", (int)result.error_code);
        exit_code = 1;
    }
    return exit_code;
}
