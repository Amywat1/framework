#include "startup/app_bootstrap.h"

#include <string.h>

#include "adapters/outbound/file_program_repository.h"
#include "application/coordinators/background_alarm_monitor.h"
#include "application/coordinators/scheduler_runtime_port.h"
#include "src/application/coordinators/device_runtime_private.h"

typedef struct app_instance_t
{
    bool initialized;
    app_state_t state;
    bool device_runtime_acquired;
    bool scheduler_created;
    device_runtime_t device_runtime;
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
    monitor_config.device_runtime = g_app_instance.device_runtime;
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
 * @brief 从当前实例状态构建状态视图。
 * @param status_view  输出状态视图，不能为空。
 */
static void build_status_view(app_status_view_t *status_view)
{
    const char *domain_reason;

    if (status_view == 0)
    {
        return;
    }

    memset(status_view, 0, sizeof(*status_view));
    status_view->state = g_app_instance.state;
    status_view->device_runtime_acquired = g_app_instance.device_runtime_acquired;
    status_view->scheduler_created = g_app_instance.scheduler_created;

    if (g_app_instance.scheduler != 0 && scheduler_read_view(g_app_instance.scheduler, &status_view->scheduler_view).ok)
    {
        status_view->scheduler_view_available = true;
    }

    if (g_app_instance.device_runtime_acquired && g_app_instance.device_runtime != 0)
    {
        domain_reason = device_runtime_last_reason_code(g_app_instance.device_runtime);
        if (domain_reason != 0 && domain_reason[0] != '\0')
        {
            status_view->domain_reason_available = true;
            strncpy(status_view->domain_last_reason_code, domain_reason,
                    sizeof(status_view->domain_last_reason_code) - 1);
            status_view->domain_last_reason_code[sizeof(status_view->domain_last_reason_code) - 1] = '\0';
        }
    }
}

/**
 * @brief 清零借用的外部资源绑定。
 * @param instance 目标运行状态结构。
 * @details 仅在资源释放后调用，用于避免悬空指针。
 */
static void zero_borrowed_bindings(app_instance_t *instance)
{
    if (instance == 0)
    {
        return;
    }

    instance->scheduler = 0;
    instance->background_alarm_monitor = 0;
    instance->device_runtime = 0;
    instance->device_runtime_acquired = false;
    instance->scheduler_created = false;
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
 * @brief 验证应用单实例是否已完成初始化。
 * @return 已初始化时返回 `ok`，否则返回失败结果。
 */
static operation_result_t require_initialized(void)
{
    if (!g_app_instance.initialized)
    {
        return operation_result_fail(ERROR_CODE_INVALID_STATE);
    }
    return operation_result_ok();
}

/**
 * @brief 释放应用实例持有的所有资源。
 * @return 全部释放成功返回 `ok`，否则返回对应错误码。
 * @details 按调度器再到 device_runtime 的顺序释放资源。
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
        device_runtime_unbind_scheduler(g_app_instance.device_runtime);
        scheduler_destroy(g_app_instance.scheduler);
        g_app_instance.scheduler = 0;
    }
    g_app_instance.scheduler_created = false;

    if (g_app_instance.device_runtime != 0)
    {
        release_result = device_runtime_release(g_app_instance.device_runtime);
        g_app_instance.device_runtime = 0;
        g_app_instance.device_runtime_acquired = false;
        if (!release_result.ok)
        {
            destroy_error_code = release_result.error_code;
        }
    }

    g_app_instance.initialized = false;
    g_app_instance.state = APP_STATE_DESTROYED;

    zero_borrowed_bindings(&g_app_instance);
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
    result = device_runtime_acquire(&g_app_instance.device_runtime);
    if (!result.ok || g_app_instance.device_runtime == 0)
    {
        error_code_t error_code = result.ok ? ERROR_CODE_RESOURCE_UNAVAILABLE : result.error_code;
        (void)destroy_owned_resources();
        return operation_result_fail(error_code);
    }
    g_app_instance.device_runtime_acquired = true;

    device_runtime_set_sensor_port(g_app_instance.device_runtime, config->sensor_port);
    device_runtime_set_actuator_port(g_app_instance.device_runtime, config->actuator_port);

    result = file_program_repository_init(g_app_instance.device_runtime, config->config_root);
    if (!result.ok)
    {
        (void)destroy_owned_resources();
        return result;
    }

    result = device_runtime_private_enter_stopped(g_app_instance.device_runtime);
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
    scheduler_runtime_port_init_from_device_runtime(&scheduler_port, g_app_instance.device_runtime);
    g_app_instance.scheduler = scheduler_create(&scheduler_port, config->scheduler_config, &scheduler_stdio);
    if (g_app_instance.scheduler == 0)
    {
        (void)destroy_owned_resources();
        return operation_result_fail(ERROR_CODE_IO_FAILED);
    }
    result = device_runtime_bind_scheduler(g_app_instance.device_runtime, g_app_instance.scheduler);
    if (!result.ok)
    {
        (void)destroy_owned_resources();
        return result;
    }

    g_app_instance.scheduler_created = true;
    g_app_instance.state = APP_STATE_CREATED;
    g_app_instance.initialized = true;
    return operation_result_ok();
}

operation_result_t app_run(void)
{
    operation_result_t result;

    result = require_initialized();
    if (!result.ok)
    {
        return result;
    }
    if (g_app_instance.state != APP_STATE_CREATED)
    {
        return operation_result_fail(ERROR_CODE_INVALID_STATE);
    }
    if (g_app_instance.scheduler == 0 || g_app_instance.device_runtime == 0)
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

operation_result_t app_read_state(app_status_view_t *status_view)
{
    if (status_view == 0)
    {
        return operation_result_fail(ERROR_CODE_INVALID_ARGUMENT);
    }
    memset(status_view, 0, sizeof(*status_view));

    if (g_app_instance.initialized)
    {
        build_status_view(status_view);
        return operation_result_ok();
    }

    status_view->state = APP_STATE_DESTROYED;
    return operation_result_ok();
}
