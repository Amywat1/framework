#include <stdio.h>
#include <string.h>

#include "platform/drivers/simulated_brush_driver.h"
#include "platform/drivers/simulated_chemical_driver.h"
#include "platform/drivers/simulated_driver_context.h"
#include "platform/drivers/simulated_dryer_driver.h"
#include "platform/drivers/simulated_gantry_driver.h"
#include "platform/drivers/simulated_ro_water_driver.h"
#include "platform/drivers/simulated_sensor_driver.h"
#include "src/startup/app_bootstrap.h"

#define CONTROL_PERIOD_MS 100ul
#define BACKGROUND_ALARM_IO_PERIOD_MS 50ul
#define BACKGROUND_ALARM_DETECT_PERIOD_MS 50ul
#define BOUNDED_DRAIN_TICKS 8u
#define MAX_TRIGGERS_PER_TICK 1u

/**
 * @brief 将仿真驱动绑定到传感器端口和执行器端口。
 * @param driver_context 仿真驱动上下文。
 * @param sensor_port 传感器端口输出位置。
 * @param actuator_port 执行器端口输出位置。
 */
static void bind_simulated_drivers(simulated_driver_context_t *driver_context, sensor_port_t *sensor_port,
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
 * @param scheduler_config  待写入的调度器配置，不能为空。
 */
static void initialize_scheduler_config(scheduler_config_t *scheduler_config)
{
    memset(scheduler_config, 0, sizeof(*scheduler_config));
    scheduler_config->control_period_ms = CONTROL_PERIOD_MS;
    scheduler_config->command_event_source_enabled = true;
    scheduler_config->notification_event_source_enabled = false;
    scheduler_config->exit_event_source_enabled = true;
    scheduler_config->exit_mode = SCHEDULER_EXIT_MODE_BOUNDED_DRAIN;
    scheduler_config->bounded_drain_ticks = BOUNDED_DRAIN_TICKS;
    scheduler_config->max_triggers_per_tick = MAX_TRIGGERS_PER_TICK;
    scheduler_config->overrun_warning_threshold_ms = CONTROL_PERIOD_MS;
}

int main(void)
{
    app_config_t app_config;
    scheduler_config_t scheduler_config;
    simulated_driver_context_t driver_context;
    sensor_port_t sensor_port;
    actuator_port_t actuator_port;
    operation_result_t result;
    int exit_code;

    bind_simulated_drivers(&driver_context, &sensor_port, &actuator_port);
    initialize_scheduler_config(&scheduler_config);
    app_config_init(&app_config);
    app_config.sensor_port = &sensor_port;
    app_config.actuator_port = &actuator_port;
    app_config.scheduler_config = &scheduler_config;
    app_config.command_input = stdin;
    app_config.command_output = stdout;
    app_config.command_error = stderr;
    app_config.config_root = "./configs";
    app_config.background_alarm_settings.enabled = true;
    app_config.background_alarm_settings.io_sample_period_ms = BACKGROUND_ALARM_IO_PERIOD_MS;
    app_config.background_alarm_settings.detect_period_ms = BACKGROUND_ALARM_DETECT_PERIOD_MS;

    result = app_create(&app_config);
    if (!result.ok)
    {
        fprintf(stderr, "App create failed, error_code=%d\n", (int)result.error_code);
        return 1;
    }

    result = app_run();
    exit_code = 0;
    if (!result.ok)
    {
        fprintf(stderr, "App run failed, error_code=%d\n", (int)result.error_code);
        exit_code = 1;
    }

    result = app_destroy();
    if (!result.ok)
    {
        fprintf(stderr, "App destroy failed, error_code=%d\n", (int)result.error_code);
        exit_code = 1;
    }
    return exit_code;
}
