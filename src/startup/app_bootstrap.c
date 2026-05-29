#include "src/startup/app_bootstrap.h"

#include <string.h>

#include "adapters/outbound/file_program_repository.h"
#include "application/coordinators/alarm_monitor.h"
#include "application/coordinators/scheduler_runtime_port.h"
#include "application/services/command_dispatch.h"
#include "platform/linux/command_ingress_stdio_linux.h"

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
    app_state_t state;
    scheduler_t *scheduler;
    alarm_monitor_t *alarm_monitor;
    command_dispatch_t command_dispatch;
    command_ingress_stdio_linux_t command_ingress;
} app_instance_t;

static app_instance_t s_app_instance;

/**
 * @brief 判断单例是否处于可再次创建状态。
 * @return 未创建或已销毁时返回 `true`。
 */
static bool bootstrap_instance_is_reusable(void)
{
    return s_app_instance.state == APP_STATE_UNAVAILABLE || s_app_instance.state == APP_STATE_DESTROYED;
}

/** @brief 判断字符串指针非空且内容非空。 */
static bool bootstrap_string_present(const char *value) { return value != 0 && value[0] != '\0'; }

/**
 * @brief 校验后台报警监控配置是否满足最小要求。
 * @param config app 创建配置。
 * @return 配置合法时返回 `operation_result_ok()`，否则返回失败结果。
 */
static operation_result_t bootstrap_validate_alarm_monitor_config(const app_config_t *config)
{
    if (config == 0)
    {
        return operation_result_fail(ERROR_CODE_INVALID_ARGUMENT);
    }
    if (!config->background_alarm_settings.enabled)
    {
        return operation_result_ok();
    }
    if (config->background_alarm_settings.io_sample_period_ms == 0ul ||
        config->background_alarm_settings.detect_period_ms == 0ul)
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
 * @brief 验证调度器配置有效性。
 * @param scheduler_config 待验证配置。
 * @return 配置合法时返回 `ok`，否则返回 `INVALID_ARGUMENT`。
 */
static operation_result_t bootstrap_validate_scheduler_config(const scheduler_config_t *scheduler_config)
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
 * @brief 校验应用创建配置是否满足最小要求。
 * @param config 待校验配置。
 * @return 配置合法时返回 `operation_result_ok()`，否则返回失败结果。
 */
static operation_result_t bootstrap_validate_config(const app_config_t *config)
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
    if (!bootstrap_string_present(config->config_root))
    {
        return operation_result_fail(ERROR_CODE_INVALID_ARGUMENT);
    }

    result = bootstrap_validate_scheduler_config(config->scheduler_config);
    if (!result.ok)
    {
        return result;
    }
    return bootstrap_validate_alarm_monitor_config(config);
}

/**
 * @brief 创建可选的后台报警监控组件。
 * @param config 应用创建配置。
 * @return 创建成功返回 `operation_result_ok()`，否则返回失败结果。
 */
static operation_result_t bootstrap_create_alarm_monitor(const app_config_t *config)
{
    alarm_monitor_config_t monitor_config;

    if (s_app_instance.alarm_monitor != 0)
    {
        return operation_result_ok();
    }
    if (!config->background_alarm_settings.enabled)
    {
        return operation_result_ok();
    }

    memset(&monitor_config, 0, sizeof(monitor_config));
    monitor_config.settings = config->background_alarm_settings;
    monitor_config.sensor_port = config->sensor_port;
    return alarm_monitor_create(&s_app_instance.alarm_monitor, &monitor_config);
}

/**
 * @brief 启动可选的后台报警监控组件。
 * @return 启动成功返回 `operation_result_ok()`，否则返回失败结果。
 */
static operation_result_t bootstrap_start_alarm_monitor(void)
{
    return alarm_monitor_start(s_app_instance.alarm_monitor);
}

/**
 * @brief 停止后台报警监控组件。
 */
static void bootstrap_stop_alarm_monitor(void) { alarm_monitor_stop(s_app_instance.alarm_monitor); }

/**
 * @brief 销毁后台报警监控组件。
 */
static void bootstrap_destroy_alarm_monitor(void)
{
    alarm_monitor_destroy(s_app_instance.alarm_monitor);
    s_app_instance.alarm_monitor = 0;
}

/**
 * @brief 释放应用实例持有的所有资源。
 * @return 全部释放成功返回 `ok`，否则返回对应错误码。
 */
static operation_result_t bootstrap_teardown_instance(void)
{
    operation_result_t deinit_result;
    error_code_t teardown_error_code;

    teardown_error_code = ERROR_CODE_OK;

    bootstrap_destroy_alarm_monitor();

    command_ingress_stdio_linux_restore(&s_app_instance.command_ingress);

    if (s_app_instance.scheduler != 0)
    {
        control_context_unbind_scheduler();
        scheduler_destroy(s_app_instance.scheduler);
        s_app_instance.scheduler = 0;
    }
    deinit_result = control_context_deinit();
    if (!deinit_result.ok)
    {
        teardown_error_code = deinit_result.error_code;
    }

    s_app_instance.state = APP_STATE_DESTROYED;
    return teardown_error_code == ERROR_CODE_OK ? operation_result_ok() : operation_result_fail(teardown_error_code);
}

void app_config_init(app_config_t *config)
{
    if (config == 0)
    {
        return;
    }
    memset(config, 0, sizeof(*config));
}

operation_result_t app_create(const app_config_t *config)
{
    command_ingress_stdio_binding_t command_ingress_stdio_binding;
    command_source_port_t command_source_port;
    scheduler_runtime_port_t scheduler_runtime_port;
    scheduler_sync_port_t scheduler_sync_port;
    command_port_t inbound_command_port;
    operation_result_t result;

    result = bootstrap_validate_config(config);
    if (!result.ok)
    {
        return result;
    }
    if (!bootstrap_instance_is_reusable())
    {
        return operation_result_fail(ERROR_CODE_RESOURCE_UNAVAILABLE);
    }

    memset(&s_app_instance, 0, sizeof(s_app_instance));
    s_app_instance.state = APP_STATE_UNAVAILABLE;
    result = control_context_init();
    if (!result.ok)
    {
        (void)bootstrap_teardown_instance();
        return result;
    }

    control_context_set_sensor_port(config->sensor_port);
    control_context_set_actuator_port(config->actuator_port);

    result = file_program_repository_init(config->config_root);
    if (!result.ok)
    {
        (void)bootstrap_teardown_instance();
        return result;
    }

    result = control_context_mark_device_ready_stopped();
    if (!result.ok)
    {
        (void)bootstrap_teardown_instance();
        return result;
    }

    result = bootstrap_create_alarm_monitor(config);
    if (!result.ok)
    {
        (void)bootstrap_teardown_instance();
        return result;
    }

    memset(&command_ingress_stdio_binding, 0, sizeof(command_ingress_stdio_binding));
    command_ingress_stdio_binding.input = config->command_input;
    command_ingress_stdio_binding.output = config->command_output;
    command_ingress_stdio_binding.error = config->command_error;
    command_ingress_stdio_linux_init(&s_app_instance.command_ingress, &command_ingress_stdio_binding);
    if (config->scheduler_config->command_event_source_enabled)
    {
        command_ingress_stdio_linux_enable(&s_app_instance.command_ingress);
    }
    command_source_port = command_ingress_stdio_linux_as_source_port(&s_app_instance.command_ingress);
    scheduler_runtime_port_init_from_control_context(&scheduler_runtime_port);
    s_app_instance.scheduler =
        scheduler_create(&scheduler_runtime_port, config->scheduler_config, &command_source_port);
    if (s_app_instance.scheduler == 0)
    {
        (void)bootstrap_teardown_instance();
        return operation_result_fail(ERROR_CODE_IO_FAILED);
    }
    result = control_context_bind_scheduler(s_app_instance.scheduler);
    if (!result.ok)
    {
        (void)bootstrap_teardown_instance();
        return result;
    }

    scheduler_sync_port = scheduler_build_sync_port(s_app_instance.scheduler);
    command_dispatch_init(&s_app_instance.command_dispatch, &scheduler_sync_port);
    inbound_command_port = command_dispatch_as_port(&s_app_instance.command_dispatch);
    scheduler_set_command_port(s_app_instance.scheduler, &inbound_command_port);

    s_app_instance.state = APP_STATE_CREATED;
    return operation_result_ok();
}

operation_result_t app_run(void)
{
    operation_result_t result;

    if (s_app_instance.state != APP_STATE_CREATED)
    {
        return operation_result_fail(ERROR_CODE_INVALID_STATE);
    }
    if (s_app_instance.scheduler == 0)
    {
        return operation_result_fail(ERROR_CODE_INVALID_STATE);
    }

    s_app_instance.state = APP_STATE_RUNNING;
    result = bootstrap_start_alarm_monitor();
    if (!result.ok)
    {
        s_app_instance.state = APP_STATE_TERMINATED;
        return result;
    }

    result = scheduler_run(s_app_instance.scheduler);
    bootstrap_stop_alarm_monitor();
    s_app_instance.state = APP_STATE_TERMINATED;
    return result;
}

operation_result_t app_destroy(void)
{
    if (bootstrap_instance_is_reusable())
    {
        return operation_result_ok();
    }
    return bootstrap_teardown_instance();
}
