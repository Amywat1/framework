#include "startup/wash_app.h"

#include <string.h>

#include "adapters/logging/file_event_logger.h"
#include "adapters/outbound/file_program_repository.h"
#include "application/coordinators/scheduler_runtime_port.h"
#include "platform/background_alarm_monitor.h"
#include "src/startup/wash_app_private.h"
#include "src/application/coordinators/device_runtime_private.h"

struct wash_app_t
{
    unsigned int reserved; /**< 占位字段，确保单实例不透明句柄结构体非空。 */
};

typedef struct wash_app_state_t
{
    wash_app_lifecycle_state_t lifecycle_state;
    bool occupied;
    bool device_runtime_acquired;
    bool scheduler_created;
    device_runtime_t device_runtime;
    controller_scheduler_t *scheduler;
    background_alarm_monitor_t *background_alarm_monitor;
} wash_app_state_t;

static wash_app_state_t g_wash_app_state;
static wash_app_t g_wash_app_handle;

static void stop_alarm_monitor(void);

/** @brief 判断字符串指针非空且内容非空。 */
static bool string_present(const char *value) { return value != 0 && value[0] != '\0'; }

/**
 * @brief 校验后台报警监控配置是否满足最小要求。
 * @param config app 创建配置。
 * @return 配置合法时返回 `operation_result_ok()`，否则返回失败结果。
 */
static operation_result_t validate_alarm_monitor_config(const wash_app_config_t *config)
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
 * @param config wash_app 创建配置。
 * @return 创建成功返回 `operation_result_ok()`，否则返回失败结果。
 */
static operation_result_t create_alarm_monitor(const wash_app_config_t *config)
{
    background_alarm_monitor_config_t monitor_config;

    if (g_wash_app_state.background_alarm_monitor != 0)
    {
        return operation_result_ok();
    }
    if (!config->background_alarm_monitor.enabled)
    {
        return operation_result_ok();
    }

    memset(&monitor_config, 0, sizeof(monitor_config));
    monitor_config.settings = config->background_alarm_monitor;
    monitor_config.device_runtime = g_wash_app_state.device_runtime;
    monitor_config.sensor_port = config->sensor_port;
    return background_alarm_monitor_create(&g_wash_app_state.background_alarm_monitor, &monitor_config);
}

/**
 * @brief 启动可选的后台报警监控组件。
 * @return 启动成功返回 `operation_result_ok()`，否则返回失败结果。
 */
static operation_result_t start_alarm_monitor(void)
{
    return background_alarm_monitor_start(g_wash_app_state.background_alarm_monitor);
}

/**
 * @brief 停止后台报警监控组件。
 */
static void stop_alarm_monitor(void)
{
    background_alarm_monitor_stop(g_wash_app_state.background_alarm_monitor);
}

/**
 * @brief 销毁后台报警监控组件。
 */
static void destroy_alarm_monitor(void)
{
    background_alarm_monitor_destroy(g_wash_app_state.background_alarm_monitor);
    g_wash_app_state.background_alarm_monitor = 0;
}

/**
 * @brief 从全局运行状态构建当前 wash_app 状态快照。
 * @param[out] status_view 输出状态视图。
 * @details 包含生命周期和调度器视图等信息。
 */
static void build_status_view(wash_app_status_view_t *status_view)
{
    if (status_view == 0)
    {
        return;
    }

    memset(status_view, 0, sizeof(*status_view));
    status_view->lifecycle_state = g_wash_app_state.lifecycle_state;
    status_view->device_runtime_acquired = g_wash_app_state.device_runtime_acquired;
    status_view->scheduler_created = g_wash_app_state.scheduler_created;

    if (g_wash_app_state.scheduler != 0 &&
        controller_scheduler_read_view(g_wash_app_state.scheduler, &status_view->scheduler_view)
            .ok)
    {
        status_view->scheduler_view_available = true;
    }
}

/**
 * @brief 清零借用的外部资源绑定。
 * @param app_state 目标运行状态结构。
 * @details 仅在资源释放后调用，用于避免悬空指针。
 */
static void zero_borrowed_bindings(wash_app_state_t *app_state)
{
    if (app_state == 0)
    {
        return;
    }

    app_state->scheduler = 0;
    app_state->background_alarm_monitor = 0;
    app_state->device_runtime = 0;
    app_state->device_runtime_acquired = false;
    app_state->scheduler_created = false;
}

/**
 * @brief 验证调度器配置有效性。
 * @param scheduler_config 待验证配置。
 * @return 配置合法时返回 `ok`，否则返回 `INVALID_ARGUMENT`。
 * @details 检查周期、最大触发数和有界排干配置是否有效。
 */
static operation_result_t validate_scheduler_config(const controller_scheduler_config_t *scheduler_config)
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
    if (scheduler_config->exit_mode == CONTROLLER_SCHEDULER_EXIT_MODE_BOUNDED_DRAIN &&
        scheduler_config->bounded_drain_ticks == 0u)
    {
        return operation_result_fail(ERROR_CODE_INVALID_ARGUMENT);
    }
    return operation_result_ok();
}

/**
 * @brief 验证句柄是否指向当前活跃 wash_app 实例。
 * @param app 待验证句柄。
 * @return 句柄合法且 app 当前被占用时返回 `ok`，否则返回失败。
 */
static operation_result_t require_current_handle(const wash_app_t *app)
{
    if (app == 0)
    {
        return operation_result_fail(ERROR_CODE_INVALID_ARGUMENT);
    }
    if (app != &g_wash_app_handle)
    {
        return operation_result_fail(ERROR_CODE_INVALID_ARGUMENT);
    }
    if (!g_wash_app_state.occupied)
    {
        return operation_result_fail(ERROR_CODE_INVALID_STATE);
    }
    return operation_result_ok();
}

/**
 * @brief 释放 wash_app 持有的所有资源。
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

    if (g_wash_app_state.scheduler != 0)
    {
        device_runtime_unbind_scheduler(g_wash_app_state.device_runtime);
        controller_scheduler_destroy(g_wash_app_state.scheduler);
        g_wash_app_state.scheduler = 0;
    }
    g_wash_app_state.scheduler_created = false;

    if (g_wash_app_state.device_runtime != 0)
    {
        release_result = device_runtime_release(g_wash_app_state.device_runtime);
        g_wash_app_state.device_runtime = 0;
        g_wash_app_state.device_runtime_acquired = false;
        if (!release_result.ok)
        {
            destroy_error_code = release_result.error_code;
        }
    }

    g_wash_app_state.occupied = false;
    g_wash_app_state.lifecycle_state = WASH_APP_STATE_DESTROYED;

    zero_borrowed_bindings(&g_wash_app_state);
    return destroy_error_code == ERROR_CODE_OK ? operation_result_ok() : operation_result_fail(destroy_error_code);
}

void wash_app_config_init(wash_app_config_t *config)
{
    if (config == 0)
    {
        return;
    }
    memset(config, 0, sizeof(*config));
}

operation_result_t wash_app_config_validate(const wash_app_config_t *config)
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
    if (!string_present(config->config_root) || !string_present(config->event_log_path))
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

operation_result_t wash_app_create(wash_app_t **app, const wash_app_config_t *config)
{
    controller_scheduler_stdio_t scheduler_stdio;
    scheduler_runtime_port_t scheduler_port;
    operation_result_t result;

    if (app == 0)
    {
        return operation_result_fail(ERROR_CODE_INVALID_ARGUMENT);
    }
    *app = 0;

    result = wash_app_config_validate(config);
    if (!result.ok)
    {
        return result;
    }
    if (g_wash_app_state.occupied)
    {
        return operation_result_fail(ERROR_CODE_RESOURCE_UNAVAILABLE);
    }

    memset(&g_wash_app_state, 0, sizeof(g_wash_app_state));
    g_wash_app_state.occupied = true;
    g_wash_app_state.lifecycle_state = WASH_APP_STATE_UNAVAILABLE;
    result = device_runtime_acquire(&g_wash_app_state.device_runtime);
    if (!result.ok || g_wash_app_state.device_runtime == 0)
    {
        error_code_t error_code = result.ok ? ERROR_CODE_RESOURCE_UNAVAILABLE : result.error_code;
        (void)destroy_owned_resources();
        return operation_result_fail(error_code);
    }
    g_wash_app_state.device_runtime_acquired = true;

    device_runtime_set_sensor_port(g_wash_app_state.device_runtime, config->sensor_port);
    device_runtime_set_actuator_port(g_wash_app_state.device_runtime, config->actuator_port);

    result = file_program_repository_init(g_wash_app_state.device_runtime, config->config_root);
    if (!result.ok)
    {
        (void)destroy_owned_resources();
        return result;
    }

    result = file_event_logger_init(g_wash_app_state.device_runtime, config->event_log_path);
    if (!result.ok)
    {
        (void)destroy_owned_resources();
        return result;
    }

    result = device_runtime_private_enter_stopped(g_wash_app_state.device_runtime);
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
    scheduler_runtime_port_init_from_device_runtime(&scheduler_port, g_wash_app_state.device_runtime);
    g_wash_app_state.scheduler = controller_scheduler_create(
        &scheduler_port, config->scheduler_config, &scheduler_stdio);
    if (g_wash_app_state.scheduler == 0)
    {
        (void)destroy_owned_resources();
        return operation_result_fail(ERROR_CODE_IO_FAILED);
    }
    result = device_runtime_bind_scheduler(g_wash_app_state.device_runtime, g_wash_app_state.scheduler);
    if (!result.ok)
    {
        (void)destroy_owned_resources();
        return result;
    }

    g_wash_app_state.scheduler_created = true;
    g_wash_app_state.lifecycle_state = WASH_APP_STATE_CREATED;
    *app = &g_wash_app_handle;
    return operation_result_ok();
}

operation_result_t wash_app_run(wash_app_t *app)
{
    operation_result_t result;

    result = require_current_handle(app);
    if (!result.ok)
    {
        return result;
    }
    if (g_wash_app_state.lifecycle_state != WASH_APP_STATE_CREATED)
    {
        return operation_result_fail(ERROR_CODE_INVALID_STATE);
    }
    if (g_wash_app_state.scheduler == 0 || g_wash_app_state.device_runtime == 0)
    {
        return operation_result_fail(ERROR_CODE_INVALID_STATE);
    }

    g_wash_app_state.lifecycle_state = WASH_APP_STATE_RUNNING;
    result = start_alarm_monitor();
    if (!result.ok)
    {
        g_wash_app_state.lifecycle_state = WASH_APP_STATE_TERMINATED;
        return result;
    }

    result = controller_scheduler_run(g_wash_app_state.scheduler);
    stop_alarm_monitor();
    g_wash_app_state.lifecycle_state = WASH_APP_STATE_TERMINATED;
    return result;
}

operation_result_t wash_app_destroy(wash_app_t *app)
{
    operation_result_t result;

    if (app == 0)
    {
        return operation_result_ok();
    }
    if (app != &g_wash_app_handle)
    {
        return operation_result_fail(ERROR_CODE_INVALID_ARGUMENT);
    }

    if (!g_wash_app_state.occupied)
    {
        return operation_result_ok();
    }

    result = require_current_handle(app);
    if (!result.ok)
    {
        return result;
    }
    return destroy_owned_resources();
}

operation_result_t wash_app_read_state(const wash_app_t *app,
                                                 wash_app_status_view_t *status_view)
{
    if (status_view == 0)
    {
        return operation_result_fail(ERROR_CODE_INVALID_ARGUMENT);
    }
    memset(status_view, 0, sizeof(*status_view));
    if (app == 0)
    {
        return operation_result_fail(ERROR_CODE_INVALID_ARGUMENT);
    }
    if (app != &g_wash_app_handle)
    {
        return operation_result_fail(ERROR_CODE_INVALID_ARGUMENT);
    }

    if (g_wash_app_state.occupied)
    {
        build_status_view(status_view);
        return operation_result_ok();
    }

    status_view->lifecycle_state = WASH_APP_STATE_DESTROYED;
    return operation_result_ok();
}

operation_result_t wash_app_private_read_device_runtime(const wash_app_t *app,
                                                                  device_runtime_t *out_device_runtime)
{
    operation_result_t result;

    if (out_device_runtime == 0)
    {
        return operation_result_fail(ERROR_CODE_INVALID_ARGUMENT);
    }
    *out_device_runtime = 0;

    result = require_current_handle(app);
    if (!result.ok)
    {
        return result;
    }
    if (!g_wash_app_state.device_runtime_acquired || g_wash_app_state.device_runtime == 0)
    {
        return operation_result_fail(ERROR_CODE_INVALID_STATE);
    }

    *out_device_runtime = g_wash_app_state.device_runtime;
    return operation_result_ok();
}

controller_scheduler_t *wash_app_private_scheduler(const wash_app_t *app)
{
    if (!require_current_handle(app).ok || !g_wash_app_state.scheduler_created)
    {
        return 0;
    }
    return g_wash_app_state.scheduler;
}
