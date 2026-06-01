#include <stdio.h>
#include <string.h>

#include "adapters/hal/sim_hw/simulated_brush_driver.h"
#include "adapters/hal/sim_hw/simulated_chemical_driver.h"
#include "adapters/hal/sim_hw/simulated_driver_context.h"
#include "adapters/hal/sim_hw/simulated_dryer_driver.h"
#include "adapters/hal/sim_hw/simulated_gantry_driver.h"
#include "adapters/hal/sim_hw/simulated_ro_water_driver.h"
#include "adapters/hal/sim_hw/simulated_sensor_driver.h"
#include "adapters/ui/cli/stdio_command_linux.h"
#include "core/bootstrap/app_bootstrap.h"

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
 * @param scheduler_config 待写入的调度器配置，不能为空。
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

/**
 * @brief 装配 Linux stdio 命令输入源并写入调度器使用的命令源端口。
 * @param scheduler_config 调度器配置，不能为空。
 * @param stdio_command stdio 命令适配器状态，不能为空。
 * @param command_source_port 输出命令源端口，不能为空。
 * @return 装配成功返回 `true`；底层 IO 配置失败时返回 `false`。
 */
static bool prepare_stdio_command_source(const scheduler_config_t *scheduler_config,
                                         stdio_command_linux_t *stdio_command,
                                         command_source_port_t *command_source_port)
{
    stdio_command_io_t stdio_command_io;

    if (scheduler_config == 0 || stdio_command == 0 || command_source_port == 0)
    {
        return false;
    }

    memset(&stdio_command_io, 0, sizeof(stdio_command_io));
    stdio_command_io.input = stdin;
    stdio_command_io.output = stdout;
    stdio_command_io.error = stderr;
    stdio_command_linux_init(stdio_command, &stdio_command_io);
    if (scheduler_config->command_event_source_enabled && stdio_command_linux_enable(stdio_command) < 0)
    {
        return false;
    }
    *command_source_port = stdio_command_linux_as_source_port(stdio_command);
    return true;
}

int main(void)
{
    app_config_t app_config;
    scheduler_config_t scheduler_config;
    simulated_driver_context_t driver_context;
    sensor_port_t sensor_port;
    actuator_port_t actuator_port;
    stdio_command_linux_t stdio_command;
    command_source_port_t command_source_port;
    operation_result_t result;
    int exit_code;

    bind_simulated_drivers(&driver_context, &sensor_port, &actuator_port);
    initialize_scheduler_config(&scheduler_config);
    if (!prepare_stdio_command_source(&scheduler_config, &stdio_command, &command_source_port))
    {
        fprintf(stderr, "Stdio command source setup failed\n");
        return 1;
    }
    app_config_init(&app_config);
    app_config.sensor_port = &sensor_port;
    app_config.actuator_port = &actuator_port;
    app_config.scheduler_config = &scheduler_config;
    app_config.command_source_port = &command_source_port;
    app_config.config_root = "./assets/configs";
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
