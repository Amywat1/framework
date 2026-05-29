#include "application/coordinators/controller_runtime.h"

#include <string.h>

#include "adapters/logging/file_event_logger.h"
#include "adapters/outbound/file_program_repository.h"
#include "platform/background_alarm_monitor.h"
#include "src/application/coordinators/controller_runtime_private.h"
#include "src/application/coordinators/system_context_private.h"

struct controller_runtime_t
{
    unsigned int reserved; /**< 占位字段，确保单实例不透明句柄结构体非空。 */
};

typedef struct controller_runtime_state_t
{
    controller_runtime_lifecycle_state_t lifecycle_state;
    bool occupied;
    bool run_invoked;
    bool system_context_acquired;
    bool scheduler_created;
    error_code_t last_error_code;
    char last_reason_code[64];
    system_context_t system_context;
    controller_scheduler_t *controller_scheduler;
    background_alarm_monitor_t *background_alarm_monitor;
    controller_runtime_config_t config_snapshot;
} controller_runtime_state_t;

static controller_runtime_state_t g_controller_runtime_state;
static controller_runtime_t g_controller_runtime_handle;

static void runtime_stop_background_alarm_monitor(void);

/** @brief 判断字符串指针非空且内容非空。 */
static bool string_present(const char *value) { return value != 0 && value[0] != '\0'; }

/**
 * @brief 校验后台报警监控配置是否满足最小要求。
 * @param config runtime 创建配置。
 * @return 配置合法时返回 `operation_result_ok()`，否则返回失败结果。
 */
static operation_result_t runtime_validate_background_alarm_monitor_config(const controller_runtime_config_t *config)
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
 * @return 创建成功返回 `operation_result_ok()`，否则返回失败结果。
 */
static operation_result_t runtime_create_background_alarm_monitor(void)
{
    background_alarm_monitor_config_t monitor_config;

    if (g_controller_runtime_state.background_alarm_monitor != 0)
    {
        return operation_result_ok();
    }
    if (!g_controller_runtime_state.config_snapshot.background_alarm_monitor.enabled)
    {
        return operation_result_ok();
    }

    memset(&monitor_config, 0, sizeof(monitor_config));
    monitor_config.settings = g_controller_runtime_state.config_snapshot.background_alarm_monitor;
    monitor_config.system_context = g_controller_runtime_state.system_context;
    monitor_config.sensor_port = g_controller_runtime_state.config_snapshot.sensor_port;
    return background_alarm_monitor_create(&g_controller_runtime_state.background_alarm_monitor, &monitor_config);
}

/**
 * @brief 启动可选的后台报警监控组件。
 * @return 启动成功返回 `operation_result_ok()`，否则返回失败结果。
 */
static operation_result_t runtime_start_background_alarm_monitor(void)
{
    return background_alarm_monitor_start(g_controller_runtime_state.background_alarm_monitor);
}

/**
 * @brief 停止后台报警监控组件。
 */
static void runtime_stop_background_alarm_monitor(void)
{
    background_alarm_monitor_stop(g_controller_runtime_state.background_alarm_monitor);
}

/**
 * @brief 销毁后台报警监控组件。
 */
static void runtime_destroy_background_alarm_monitor(void)
{
    background_alarm_monitor_destroy(g_controller_runtime_state.background_alarm_monitor);
    g_controller_runtime_state.background_alarm_monitor = 0;
}

/**
 * @brief 从 system_context 复制最近原因码到 runtime_state。
 * @param runtime_state 目标运行状态结构。
 * @note 复制前会先清空本地缓冲区。
 */
static void runtime_copy_last_reason_from_context(controller_runtime_state_t *runtime_state)
{
    const char *reason_code;

    if (runtime_state == 0)
    {
        return;
    }

    memset(runtime_state->last_reason_code, 0, sizeof(runtime_state->last_reason_code));
    if (runtime_state->system_context == 0)
    {
        return;
    }

    reason_code = system_context_last_reason_code(runtime_state->system_context);
    if (reason_code != 0)
    {
        strncpy(runtime_state->last_reason_code, reason_code, sizeof(runtime_state->last_reason_code) - 1u);
    }
}

/**
 * @brief 从全局运行状态构建当前 runtime 的状态快照。
 * @param[out] status_view 输出状态视图。
 * @details 包含生命周期、错误码、原因码和调度器视图等信息。
 */
static void runtime_build_status_view(controller_runtime_status_view_t *status_view)
{
    if (status_view == 0)
    {
        return;
    }

    memset(status_view, 0, sizeof(*status_view));
    status_view->lifecycle_state = g_controller_runtime_state.lifecycle_state;
    status_view->system_context_acquired = g_controller_runtime_state.system_context_acquired;
    status_view->scheduler_created = g_controller_runtime_state.scheduler_created;
    status_view->run_invoked = g_controller_runtime_state.run_invoked;
    status_view->last_error_code = g_controller_runtime_state.last_error_code;
    /* 预留 1 字节给 null 终止符，避免缓冲区越界。 */
    strncpy(status_view->last_reason_code, g_controller_runtime_state.last_reason_code,
            sizeof(status_view->last_reason_code) - 1u);

    if (g_controller_runtime_state.controller_scheduler != 0 &&
        controller_scheduler_read_view(g_controller_runtime_state.controller_scheduler, &status_view->scheduler_view)
            .ok)
    {
        status_view->scheduler_view_available = true;
    }
}

/**
 * @brief 为已销毁 runtime 构建状态视图。
 * @param[out] status_view 输出状态视图。
 * @param last_error_code 最后的错误码。
 * @param last_reason_code 最后的原因码。
 * @details 生命周期会标记为 `CONTROLLER_RUNTIME_STATE_DESTROYED`。
 */
static void runtime_build_destroyed_status_view(controller_runtime_status_view_t *status_view,
                                                error_code_t last_error_code, const char *last_reason_code)
{
    if (status_view == 0)
    {
        return;
    }

    memset(status_view, 0, sizeof(*status_view));
    status_view->lifecycle_state = CONTROLLER_RUNTIME_STATE_DESTROYED;
    status_view->last_error_code = last_error_code;
    if (last_reason_code != 0)
    {
        strncpy(status_view->last_reason_code, last_reason_code, sizeof(status_view->last_reason_code) - 1u);
    }
}

/**
 * @brief 清零借用的外部资源绑定。
 * @param runtime_state 目标运行状态结构。
 * @details 仅在资源释放后调用，用于避免悬空指针。
 */
static void runtime_zero_borrowed_bindings(controller_runtime_state_t *runtime_state)
{
    if (runtime_state == 0)
    {
        return;
    }

    memset(&runtime_state->config_snapshot, 0, sizeof(runtime_state->config_snapshot));
    runtime_state->controller_scheduler = 0;
    runtime_state->background_alarm_monitor = 0;
    runtime_state->system_context = 0;
    runtime_state->system_context_acquired = false;
    runtime_state->scheduler_created = false;
}

/**
 * @brief 验证调度器配置有效性。
 * @param scheduler_config 待验证配置。
 * @return 配置合法时返回 `ok`，否则返回 `INVALID_ARGUMENT`。
 * @details 检查周期、最大触发数和有界排干配置是否有效。
 */
static operation_result_t runtime_validate_scheduler_config(const controller_scheduler_config_t *scheduler_config)
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
 * @brief 验证句柄是否指向当前活跃 runtime。
 * @param runtime 待验证句柄。
 * @return 句柄合法且 runtime 当前被占用时返回 `ok`，否则返回失败。
 */
static operation_result_t runtime_require_current_handle(const controller_runtime_t *runtime)
{
    if (runtime == 0)
    {
        return operation_result_fail(ERROR_CODE_INVALID_ARGUMENT);
    }
    if (runtime != &g_controller_runtime_handle)
    {
        return operation_result_fail(ERROR_CODE_INVALID_ARGUMENT);
    }
    if (!g_controller_runtime_state.occupied)
    {
        return operation_result_fail(ERROR_CODE_INVALID_STATE);
    }
    return operation_result_ok();
}

/**
 * @brief 释放 runtime 持有的所有资源。
 * @return 全部释放成功返回 `ok`，否则返回对应错误码。
 * @details 按调度器再到 system_context 的顺序释放资源。
 */
static operation_result_t runtime_destroy_owned_resources(void)
{
    operation_result_t release_result;
    error_code_t destroy_error_code;

    runtime_copy_last_reason_from_context(&g_controller_runtime_state);
    g_controller_runtime_state.last_error_code = ERROR_CODE_OK;
    destroy_error_code = ERROR_CODE_OK;
    release_result = operation_result_ok();

    runtime_destroy_background_alarm_monitor();

    if (g_controller_runtime_state.controller_scheduler != 0)
    {
        controller_scheduler_destroy(g_controller_runtime_state.controller_scheduler);
        g_controller_runtime_state.controller_scheduler = 0;
    }
    g_controller_runtime_state.scheduler_created = false;

    if (g_controller_runtime_state.system_context != 0)
    {
        release_result = system_context_release(g_controller_runtime_state.system_context);
        g_controller_runtime_state.system_context = 0;
        g_controller_runtime_state.system_context_acquired = false;
        if (!release_result.ok)
        {
            g_controller_runtime_state.last_error_code = release_result.error_code;
            destroy_error_code = release_result.error_code;
        }
    }

    g_controller_runtime_state.occupied = false;
    g_controller_runtime_state.lifecycle_state = CONTROLLER_RUNTIME_STATE_DESTROYED;

    runtime_zero_borrowed_bindings(&g_controller_runtime_state);
    return destroy_error_code == ERROR_CODE_OK ? operation_result_ok() : operation_result_fail(destroy_error_code);
}

void controller_runtime_config_init(controller_runtime_config_t *config)
{
    if (config == 0)
    {
        return;
    }
    memset(config, 0, sizeof(*config));
}

operation_result_t controller_runtime_config_validate(const controller_runtime_config_t *config)
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

    result = runtime_validate_scheduler_config(config->scheduler_config);
    if (!result.ok)
    {
        return result;
    }
    result = runtime_validate_background_alarm_monitor_config(config);
    if (!result.ok)
    {
        return result;
    }
    return operation_result_ok();
}

operation_result_t controller_runtime_create(controller_runtime_t **runtime, const controller_runtime_config_t *config)
{
    controller_scheduler_stdio_t scheduler_stdio;
    operation_result_t result;

    if (runtime == 0)
    {
        return operation_result_fail(ERROR_CODE_INVALID_ARGUMENT);
    }
    *runtime = 0;

    result = controller_runtime_config_validate(config);
    if (!result.ok)
    {
        return result;
    }
    if (g_controller_runtime_state.occupied)
    {
        return operation_result_fail(ERROR_CODE_RESOURCE_UNAVAILABLE);
    }

    memset(&g_controller_runtime_state, 0, sizeof(g_controller_runtime_state));
    g_controller_runtime_state.occupied = true;
    g_controller_runtime_state.lifecycle_state = CONTROLLER_RUNTIME_STATE_UNAVAILABLE;
    g_controller_runtime_state.last_error_code = ERROR_CODE_OK;
    g_controller_runtime_state.config_snapshot = *config;

    result = system_context_acquire(&g_controller_runtime_state.system_context);
    if (!result.ok || g_controller_runtime_state.system_context == 0)
    {
        g_controller_runtime_state.last_error_code = result.ok ? ERROR_CODE_RESOURCE_UNAVAILABLE : result.error_code;
        (void)runtime_destroy_owned_resources();
        return operation_result_fail(g_controller_runtime_state.last_error_code);
    }
    g_controller_runtime_state.system_context_acquired = true;

    system_context_set_sensor_port(g_controller_runtime_state.system_context, config->sensor_port);
    system_context_set_actuator_port(g_controller_runtime_state.system_context, config->actuator_port);

    result = file_program_repository_init(g_controller_runtime_state.system_context, config->config_root);
    if (!result.ok)
    {
        g_controller_runtime_state.last_error_code = result.error_code;
        runtime_copy_last_reason_from_context(&g_controller_runtime_state);
        (void)runtime_destroy_owned_resources();
        return result;
    }

    result = file_event_logger_init(g_controller_runtime_state.system_context, config->event_log_path);
    if (!result.ok)
    {
        g_controller_runtime_state.last_error_code = result.error_code;
        runtime_copy_last_reason_from_context(&g_controller_runtime_state);
        (void)runtime_destroy_owned_resources();
        return result;
    }

    result = system_context_private_complete_initialization(g_controller_runtime_state.system_context);
    if (!result.ok)
    {
        g_controller_runtime_state.last_error_code = result.error_code;
        runtime_copy_last_reason_from_context(&g_controller_runtime_state);
        (void)runtime_destroy_owned_resources();
        return result;
    }

    result = runtime_create_background_alarm_monitor();
    if (!result.ok)
    {
        g_controller_runtime_state.last_error_code = result.error_code;
        runtime_copy_last_reason_from_context(&g_controller_runtime_state);
        (void)runtime_destroy_owned_resources();
        return result;
    }

    memset(&scheduler_stdio, 0, sizeof(scheduler_stdio));
    scheduler_stdio.input = config->command_input;
    scheduler_stdio.output = config->command_output;
    scheduler_stdio.error = config->command_error;
    g_controller_runtime_state.controller_scheduler = controller_scheduler_create(
        g_controller_runtime_state.system_context, config->scheduler_config, &scheduler_stdio);
    if (g_controller_runtime_state.controller_scheduler == 0)
    {
        g_controller_runtime_state.last_error_code = ERROR_CODE_IO_FAILED;
        runtime_copy_last_reason_from_context(&g_controller_runtime_state);
        (void)runtime_destroy_owned_resources();
        return operation_result_fail(ERROR_CODE_IO_FAILED);
    }

    g_controller_runtime_state.scheduler_created = true;
    g_controller_runtime_state.lifecycle_state = CONTROLLER_RUNTIME_STATE_CREATED;
    runtime_copy_last_reason_from_context(&g_controller_runtime_state);
    *runtime = &g_controller_runtime_handle;
    return operation_result_ok();
}

operation_result_t controller_runtime_run(controller_runtime_t *runtime)
{
    operation_result_t result;

    result = runtime_require_current_handle(runtime);
    if (!result.ok)
    {
        return result;
    }
    if (g_controller_runtime_state.lifecycle_state != CONTROLLER_RUNTIME_STATE_CREATED)
    {
        return operation_result_fail(ERROR_CODE_INVALID_STATE);
    }
    if (g_controller_runtime_state.controller_scheduler == 0 || g_controller_runtime_state.system_context == 0)
    {
        return operation_result_fail(ERROR_CODE_INVALID_STATE);
    }

    g_controller_runtime_state.lifecycle_state = CONTROLLER_RUNTIME_STATE_RUNNING;
    g_controller_runtime_state.run_invoked = true;
    result = runtime_start_background_alarm_monitor();
    if (!result.ok)
    {
        g_controller_runtime_state.last_error_code = result.error_code;
        runtime_copy_last_reason_from_context(&g_controller_runtime_state);
        g_controller_runtime_state.lifecycle_state = CONTROLLER_RUNTIME_STATE_TERMINATED;
        return result;
    }

    result = controller_scheduler_run(g_controller_runtime_state.controller_scheduler);
    runtime_stop_background_alarm_monitor();
    g_controller_runtime_state.last_error_code = result.error_code;
    runtime_copy_last_reason_from_context(&g_controller_runtime_state);
    g_controller_runtime_state.lifecycle_state = CONTROLLER_RUNTIME_STATE_TERMINATED;
    return result;
}

operation_result_t controller_runtime_destroy(controller_runtime_t *runtime)
{
    operation_result_t result;

    if (runtime == 0)
    {
        return operation_result_ok();
    }
    if (runtime != &g_controller_runtime_handle)
    {
        return operation_result_fail(ERROR_CODE_INVALID_ARGUMENT);
    }

    if (!g_controller_runtime_state.occupied)
    {
        return operation_result_ok();
    }

    result = runtime_require_current_handle(runtime);
    if (!result.ok)
    {
        return result;
    }
    return runtime_destroy_owned_resources();
}

operation_result_t controller_runtime_read_state(const controller_runtime_t *runtime,
                                                 controller_runtime_status_view_t *status_view)
{
    if (status_view == 0)
    {
        return operation_result_fail(ERROR_CODE_INVALID_ARGUMENT);
    }
    memset(status_view, 0, sizeof(*status_view));
    if (runtime == 0)
    {
        return operation_result_fail(ERROR_CODE_INVALID_ARGUMENT);
    }
    if (runtime != &g_controller_runtime_handle)
    {
        return operation_result_fail(ERROR_CODE_INVALID_ARGUMENT);
    }

    if (g_controller_runtime_state.occupied)
    {
        runtime_build_status_view(status_view);
        return operation_result_ok();
    }

    runtime_build_destroyed_status_view(status_view, ERROR_CODE_OK, 0);
    return operation_result_ok();
}

operation_result_t controller_runtime_private_read_system_context(const controller_runtime_t *runtime,
                                                                  system_context_t *system_context)
{
    operation_result_t result;

    if (system_context == 0)
    {
        return operation_result_fail(ERROR_CODE_INVALID_ARGUMENT);
    }
    *system_context = 0;

    result = runtime_require_current_handle(runtime);
    if (!result.ok)
    {
        return result;
    }
    if (!g_controller_runtime_state.system_context_acquired || g_controller_runtime_state.system_context == 0)
    {
        return operation_result_fail(ERROR_CODE_INVALID_STATE);
    }

    *system_context = g_controller_runtime_state.system_context;
    return operation_result_ok();
}

controller_scheduler_t *controller_runtime_private_scheduler(const controller_runtime_t *runtime)
{
    if (!runtime_require_current_handle(runtime).ok || !g_controller_runtime_state.scheduler_created)
    {
        return 0;
    }
    return g_controller_runtime_state.controller_scheduler;
}
