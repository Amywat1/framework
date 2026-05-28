#include "startup/app_bootstrap.h"

#include <string.h>

#include "adapters/outbound/file_program_repository.h"
#include "application/coordinators/background_alarm_monitor.h"
#include "application/coordinators/scheduler_runtime_port.h"
#include "src/application/coordinators/control_context_private.h"

typedef enum app_state_t
{
    APP_STATE_UNAVAILABLE = 0,
    APP_STATE_CREATED,
    APP_STATE_RUNNING,
    APP_STATE_TERMINATED,
    APP_STATE_DESTROYED
} app_state_t;

typedef struct app_instance_t
{
    bool initialized;
    app_state_t state;
    scheduler_t *scheduler;
    background_alarm_monitor_t *background_alarm_monitor;
} app_instance_t;

static app_instance_t g_app_instance;

static void stop_alarm_monitor(void);

/** @brief 判断字符串指针非空且内容非空。 */
static bool string_present(const char *value) { return value != 0 && value[0] != '\0'; }

/**
 * @brief 校验后台报警监控配置是否满足最小要求。
 * @param config app 创建配置。
 * @return 配置合法时返回 `operation_result_ok()`，否则返回失败结果。
 */
static operation_result_t validate_alarm_monitor_config(const app_config_t *config)
{
    if (config == 0)
    {
        return operation_result_fail(ERROR_CODE_INVALID_ARGUMENT);
    }
    if (!config->background_alarm_monitor.enabled)
    {
        return operation_result_ok();
    }
    if (config->background_alarm_monitor.io_sample_period_ms == 0ul ||
        config->background_alarm_monitor.detect_period_ms == 0ul)
    {
        return operation_result_fail(ERROR_CODE_INVALID_ARGUMENT);
    }
    if (config->sensor_port == 0 || config->sensor_port->read_snapshot == 0)
    {
        return operation_result_fail(ERROR_CODE_INVALID_ARGUMENT);
    }
    return operation_result_ok();
}

/**
 * @brief 创建可选的后台报警监控组件。
 * @param config  应用创建配置。
 * @return 创建成功返回 `operation_result_ok()`，否则返回失败结果。
 */
static operation_result_t create_alarm_monitor(const app_config_t *config)
{
    background_alarm_monitor_config_t monitor_config;

    if (g_app_instance.background_alarm_monitor != 0)
    {
        return operation_result_ok();
    }
    if (!config->background_alarm_monitor.enabled)
    {
        return operation_result_ok();
    }

    memset(&monitor_config, 0, sizeof(monitor_config));
    monitor_config.settings = config->background_alarm_monitor;
    monitor_config.sensor_port = config->sensor_port;
    return background_alarm_monitor_create(&g_app_instance.background_alarm_monitor, &monitor_config);
}

/**
 * @brief 启动可选的后台报警监控组件。
 * @return 启动成功返回 `operation_result_ok()`，否则返回失败结果。
 */
static operation_result_t start_alarm_monitor(void)
{
    return background_alarm_monitor_start(g_app_instance.background_alarm_monitor);
}

/**
 * @brief 停止后台报警监控组件。
 */
static void stop_alarm_monitor(void) { background_alarm_monitor_stop(g_app_instance.background_alarm_monitor); }

/**
 * @brief 销毁后台报警监控组件。
 */
static void destroy_alarm_monitor(void)
{
    background_alarm_monitor_destroy(g_app_instance.background_alarm_monitor);
    g_app_instance.background_alarm_monitor = 0;
}

/**
 * @brief 验证调度器配置有效性。
 * @param scheduler_config 待验证配置。
 * @return 配置合法时返回 `ok`，否则返回 `INVALID_ARGUMENT`。
 * @details 检查周期、最大触发数和有界排干配置是否有效。
 */
static operation_result_t validate_scheduler_config(const scheduler_config_t *scheduler_config)
{
    if (scheduler_config == 0)
    {
        return operation_result_fail(ERROR_CODE_INVALID_ARGUMENT);
    }
    if (scheduler_config->control_period_ms == 0ul)
    {
        return operation_result_fail(ERROR_CODE_INVALID_ARGUMENT);
    }
    if (scheduler_config->max_triggers_per_tick == 0u)
    {
        return operation_result_fail(ERROR_CODE_INVALID_ARGUMENT);
    }
    if (scheduler_config->exit_mode == SCHEDULER_EXIT_MODE_BOUNDED_DRAIN && scheduler_config->bounded_drain_ticks == 0u)
    {
        return operation_result_fail(ERROR_CODE_INVALID_ARGUMENT);
    }
    return operation_result_ok();
}

/**
 * @brief 释放应用实例持有的所有资源。
 * @return 全部释放成功返回 `ok`，否则返回对应错误码。
 * @details 按调度器再到 control_context 的顺序释放资源。
 */
static operation_result_t destroy_owned_resources(void)
{
    operation_result_t release_result;
    error_code_t destroy_error_code;

    destroy_error_code = ERROR_CODE_OK;
    release_result = operation_result_ok();

    destroy_alarm_monitor();

    if (g_app_instance.scheduler != 0)
    {
        control_context_unbind_scheduler();
        scheduler_destroy(g_app_instance.scheduler);
        g_app_instance.scheduler = 0;
    }
    release_result = control_context_deinit();
    if (!release_result.ok)
    {
        destroy_error_code = release_result.error_code;
    }

    g_app_instance.initialized = false;
    g_app_instance.state = APP_STATE_DESTROYED;
    return destroy_error_code == ERROR_CODE_OK ? operation_result_ok() : operation_result_fail(destroy_error_code);
}

void app_config_init(app_config_t *config)
{
    if (config == 0)
    {
        return;
    }
    memset(config, 0, sizeof(*config));
}

operation_result_t app_config_validate(const app_config_t *config)
{
    operation_result_t result;

    if (config == 0)
    {
        return operation_result_fail(ERROR_CODE_INVALID_ARGUMENT);
    }
    if (config->sensor_port == 0 || config->actuator_port == 0)
    {
        return operation_result_fail(ERROR_CODE_INVALID_ARGUMENT);
    }
    if (!string_present(config->config_root))
    {
        return operation_result_fail(ERROR_CODE_INVALID_ARGUMENT);
    }

    result = validate_scheduler_config(config->scheduler_config);
    if (!result.ok)
    {
        return result;
    }
    result = validate_alarm_monitor_config(config);
    if (!result.ok)
    {
        return result;
    }
    return operation_result_ok();
}

operation_result_t app_create(const app_config_t *config)
{
    scheduler_stdio_t scheduler_stdio;
    scheduler_runtime_port_t scheduler_port;
    operation_result_t result;

    result = app_config_validate(config);
    if (!result.ok)
    {
        return result;
    }
    if (g_app_instance.initialized)
    {
        return operation_result_fail(ERROR_CODE_RESOURCE_UNAVAILABLE);
    }

    memset(&g_app_instance, 0, sizeof(g_app_instance));
    g_app_instance.state = APP_STATE_UNAVAILABLE;
    result = control_context_init();
    if (!result.ok)
    {
        (void)destroy_owned_resources();
        return result;
    }

    control_context_set_sensor_port(config->sensor_port);
    control_context_set_actuator_port(config->actuator_port);

    result = file_program_repository_init(config->config_root);
    if (!result.ok)
    {
        (void)destroy_owned_resources();
        return result;
    }

    result = control_context_private_enter_stopped();
    if (!result.ok)
    {
        (void)destroy_owned_resources();
        return result;
    }

    result = create_alarm_monitor(config);
    if (!result.ok)
    {
        (void)destroy_owned_resources();
        return result;
    }

    memset(&scheduler_stdio, 0, sizeof(scheduler_stdio));
    scheduler_stdio.input = config->command_input;
    scheduler_stdio.output = config->command_output;
    scheduler_stdio.error = config->command_error;
    scheduler_runtime_port_init_from_control_context(&scheduler_port);
    g_app_instance.scheduler = scheduler_create(&scheduler_port, config->scheduler_config, &scheduler_stdio);
    if (g_app_instance.scheduler == 0)
    {
        (void)destroy_owned_resources();
        return operation_result_fail(ERROR_CODE_IO_FAILED);
    }
    result = control_context_bind_scheduler(g_app_instance.scheduler);
    if (!result.ok)
    {
        (void)destroy_owned_resources();
        return result;
    }

    g_app_instance.state = APP_STATE_CREATED;
    g_app_instance.initialized = true;
    return operation_result_ok();
}

operation_result_t app_run(void)
{
    operation_result_t result;

    if (!g_app_instance.initialized)
    {
        return operation_result_fail(ERROR_CODE_INVALID_STATE);
    }
    if (g_app_instance.state != APP_STATE_CREATED)
    {
        return operation_result_fail(ERROR_CODE_INVALID_STATE);
    }
    if (g_app_instance.scheduler == 0)
    {
        return operation_result_fail(ERROR_CODE_INVALID_STATE);
    }

    g_app_instance.state = APP_STATE_RUNNING;
    result = start_alarm_monitor();
    if (!result.ok)
    {
        g_app_instance.state = APP_STATE_TERMINATED;
        return result;
    }

    result = scheduler_run(g_app_instance.scheduler);
    stop_alarm_monitor();
    g_app_instance.state = APP_STATE_TERMINATED;
    return result;
}

operation_result_t app_destroy(void)
{
    if (!g_app_instance.initialized)
    {
        return operation_result_ok();
    }
    return destroy_owned_resources();
}

