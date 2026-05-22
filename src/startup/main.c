#include <stdio.h>
#include <string.h>

#include "startup/wash_app.h"
#include "platform/drivers/simulated_brush_driver.h"
#include "platform/drivers/simulated_chemical_driver.h"
#include "platform/drivers/simulated_driver_context.h"
#include "platform/drivers/simulated_dryer_driver.h"
#include "platform/drivers/simulated_gantry_driver.h"
#include "platform/drivers/simulated_ro_water_driver.h"
#include "platform/drivers/simulated_sensor_driver.h"

#define CONTROLLER_CONTROL_PERIOD_MS 100ul
#define CONTROLLER_BACKGROUND_ALARM_IO_PERIOD_MS 50ul
#define CONTROLLER_BACKGROUND_ALARM_DETECT_PERIOD_MS 50ul
#define CONTROLLER_BOUNDED_DRAIN_TICKS 8u
#define CONTROLLER_MAX_TRIGGERS_PER_TICK 1u

/**
 * @brief 初始化仿真传感器端口和执行器端口。
 * @param driver_context 仿真驱动上下文。
 * @param sensor_port 传感器端口输出位置。
 * @param actuator_port 执行器端口输出位置。
 */
static void initialize_ports(simulated_driver_context_t *driver_context, sensor_port_t *sensor_port,
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

/**
 * @brief 初始化控制器调度器配置。
 * @param scheduler_config 待写入的调度器配置。
 */
static void initialize_scheduler_config(controller_scheduler_config_t *scheduler_config)
{
    memset(scheduler_config, 0, sizeof(*scheduler_config));
    scheduler_config->control_period_ms = CONTROLLER_CONTROL_PERIOD_MS;
    scheduler_config->command_event_source_enabled = true;
    scheduler_config->notification_event_source_enabled = false;
    scheduler_config->exit_event_source_enabled = true;
    scheduler_config->exit_mode = CONTROLLER_SCHEDULER_EXIT_MODE_BOUNDED_DRAIN;
    scheduler_config->bounded_drain_ticks = CONTROLLER_BOUNDED_DRAIN_TICKS;
    scheduler_config->max_triggers_per_tick = CONTROLLER_MAX_TRIGGERS_PER_TICK;
    scheduler_config->overrun_warning_threshold_ms = CONTROLLER_CONTROL_PERIOD_MS;
    scheduler_config->observability_enabled = true;
}

int main(void)
{
    wash_app_status_view_t runtime_status_view;
    wash_app_config_t controller_runtime_config;
    controller_scheduler_config_t scheduler_config;
    simulated_driver_context_t driver_context;
    wash_app_t *controller_runtime;
    sensor_port_t sensor_port;
    actuator_port_t actuator_port;
    operation_result_t result;
    int exit_code;

    initialize_ports(&driver_context, &sensor_port, &actuator_port);
    initialize_scheduler_config(&scheduler_config);
    wash_app_config_init(&controller_runtime_config);
    controller_runtime_config.sensor_port = &sensor_port;
    controller_runtime_config.actuator_port = &actuator_port;
    controller_runtime_config.scheduler_config = &scheduler_config;
    controller_runtime_config.command_input = stdin;
    controller_runtime_config.command_output = stdout;
    controller_runtime_config.command_error = stderr;
    controller_runtime_config.config_root = "./configs";
    controller_runtime_config.event_log_path = "./runtime/logs/events.log";
    controller_runtime_config.background_alarm_monitor.enabled = true;
    controller_runtime_config.background_alarm_monitor.io_sample_period_ms = CONTROLLER_BACKGROUND_ALARM_IO_PERIOD_MS;
    controller_runtime_config.background_alarm_monitor.detect_period_ms = CONTROLLER_BACKGROUND_ALARM_DETECT_PERIOD_MS;

    result = wash_app_create(&controller_runtime, &controller_runtime_config);
    if (!result.ok || controller_runtime == 0)
    {
        fprintf(stderr, "wash_controller: create failed, error_code=%d\n", (int)result.error_code);
        return 1;
    }

    result = wash_app_run(controller_runtime);
    exit_code = 0;
    if (!result.ok)
    {
        const char *scheduler_reason = "none";
        const char *domain_reason = "none";

        (void)wash_app_read_state(controller_runtime, &runtime_status_view);
        if (runtime_status_view.scheduler_view_available &&
            runtime_status_view.scheduler_view.metrics.last_error_reason[0] != '\0')
        {
            scheduler_reason = runtime_status_view.scheduler_view.metrics.last_error_reason;
        }
        if (runtime_status_view.last_reason_code[0] != '\0')
        {
            domain_reason = runtime_status_view.last_reason_code;
        }
        fprintf(stderr, "wash_controller: run failed, scheduler_reason=%s, domain_reason=%s\n", scheduler_reason,
                domain_reason);
        exit_code = 1;
    }

    result = wash_app_destroy(controller_runtime);
    if (!result.ok)
    {
        fprintf(stderr, "wash_controller: destroy failed, error_code=%d\n", (int)result.error_code);
        exit_code = 1;
    }
    return exit_code;
}
