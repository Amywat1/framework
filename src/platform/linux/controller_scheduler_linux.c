#define _GNU_SOURCE
#define _POSIX_C_SOURCE 200809L

#include "platform/linux/controller_scheduler_linux.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/eventfd.h>
#include <sys/timerfd.h>
#include <time.h>
#include <unistd.h>

#include "domain/model/domain_enums.h"
#include "shared/error_codes.h"
#include "src/platform/linux/controller_scheduler_linux_internal.h"

#define CONTROLLER_SCHEDULER_MAX_EPOLL_EVENTS 4

/**
 * @brief 读取单调时钟的当前时刻（毫秒）
 * @return 毫秒时刻；系统调用失败时返回 0ul（降级处理）
 * @details 用 CLOCK_MONOTONIC 避免系统时间调整影响；不受 NTP 修正影响
 */
static unsigned long monotonic_now_ms(void)
{
    struct timespec current_time;

    if (clock_gettime(CLOCK_MONOTONIC, &current_time) != 0)
    {
        return 0ul;
    }
    return (unsigned long)(current_time.tv_sec * 1000ul) + (unsigned long)(current_time.tv_nsec / 1000000ul);
}

/**
 * @brief 计算调度器启动以来经过的毫秒数
 * @param controller_scheduler 调度器实例
 * @return 从 epoch 至今的经过时间；epoch 未设置或时间倒退时返回 0ul
 */
static unsigned long controller_scheduler_elapsed_ms(const controller_scheduler_t *controller_scheduler)
{
    unsigned long now_ms;

    now_ms = monotonic_now_ms();
    if (now_ms < controller_scheduler->monotonic_epoch_ms)
    {
        return 0ul;
    }
    return now_ms - controller_scheduler->monotonic_epoch_ms;
}

/**
 * @brief 安全地关闭文件描述符
 * @param fd 指向文件描述符的指针
 * @details 检查指针和有效性，关闭后置为 -1；允许多次调用（幂等）
 */
static void close_fd_if_needed(int *fd)
{
    if (fd != 0 && *fd >= 0)
    {
        close(*fd);
        *fd = -1;
    }
}

/**
 * @brief 根据配置状态确定事件源的初始状态
 * @param enabled 配置中是否启用此事件源
 * @return 启用时返回 DEGRADED（文件描述符可能还未就绪），禁用时返回 DISABLED
 */
static controller_scheduler_event_source_state_t controller_scheduler_configured_source_state(bool enabled)
{
    return enabled ? CONTROLLER_SCHEDULER_EVENT_SOURCE_DEGRADED : CONTROLLER_SCHEDULER_EVENT_SOURCE_DISABLED;
}

/**
 * @brief 刷新各事件源的状态
 * @param controller_scheduler 调度器实例
 * @details 根据配置和资源可用性更新事件源状态（启用/降级/禁用）；CLOSED 状态不可更改
 */
static void controller_scheduler_refresh_source_states(controller_scheduler_t *controller_scheduler)
{
    if (controller_scheduler == 0)
    {
        return;
    }

    if (controller_scheduler->command_source.source_state != CONTROLLER_SCHEDULER_EVENT_SOURCE_CLOSED)
    {
        controller_scheduler->command_source.source_state =
            controller_scheduler->config.command_event_source_enabled
                ? (stdio_formal_command_adapter_fd(&controller_scheduler->command_adapter) >= 0
                       ? CONTROLLER_SCHEDULER_EVENT_SOURCE_ENABLED
                       : CONTROLLER_SCHEDULER_EVENT_SOURCE_DEGRADED)
                : CONTROLLER_SCHEDULER_EVENT_SOURCE_DISABLED;
    }
    controller_scheduler->notification_source.source_state =
        controller_scheduler_configured_source_state(controller_scheduler->config.notification_event_source_enabled);
    controller_scheduler->exit_source.source_state =
        controller_scheduler_configured_source_state(controller_scheduler->config.exit_event_source_enabled);
}

/**
 * @brief 记录最近一次错误原因，并按需将调度器标记为失败态。
 * @param controller_scheduler 调度器实例。
 * @param reason_code 错误原因码。
 * @param terminal 是否同步切换到失败态。
 */
void controller_scheduler_record_error(controller_scheduler_t *controller_scheduler, const char *reason_code,
                                       bool terminal)
{
    if (controller_scheduler == 0 || reason_code == 0)
    {
        return;
    }

    strncpy(controller_scheduler->metrics.last_error_reason, reason_code,
            sizeof(controller_scheduler->metrics.last_error_reason) - 1);
    controller_scheduler->metrics.last_error_reason[sizeof(controller_scheduler->metrics.last_error_reason) - 1] = '\0';
    if (terminal)
    {
        controller_scheduler->runtime_state = CONTROLLER_SCHEDULER_RUNTIME_STATE_FAILED;
    }
}

/**
 * @brief 在可观测性开启时写入调度器日志消息。
 * @param controller_scheduler 调度器实例。
 * @param message 待写入的日志文本。
 */
static void controller_scheduler_log_message(controller_scheduler_t *controller_scheduler, const char *message)
{
    const event_logger_port_t *event_logger_port;

    if (controller_scheduler == 0 || message == 0)
    {
        return;
    }
    if (!controller_scheduler->config.observability_enabled)
    {
        return;
    }
    event_logger_port = controller_scheduler->runtime_port.event_logger_port(
        controller_scheduler->runtime_port.context);
    if (event_logger_port != 0 && event_logger_port->log_message != 0)
    {
        event_logger_port->log_message(event_logger_port->context, TRIGGER_TYPE_BUSINESS, message);
    }
}

/**
 * @brief 刷新待处理触发数指标。
 * @param controller_scheduler 调度器实例。
 */
void controller_scheduler_update_pending_metric(controller_scheduler_t *controller_scheduler)
{
    if (controller_scheduler == 0)
    {
        return;
    }
    controller_scheduler->metrics.pending_trigger_count =
        controller_scheduler->runtime_port.pending_trigger_count(controller_scheduler->runtime_port.context);
}

/**
 * @brief 记录事件源的触发次数与最近观测时间。
 * @param source_descriptor 事件源描述符。
 * @param event_count 本次新增事件数。
 * @param seen_time_ms 最近观测时间。
 */
static void controller_scheduler_note_source_event(scheduler_event_source_descriptor_t *source_descriptor,
                                                   unsigned long event_count, unsigned long seen_time_ms)
{
    if (source_descriptor == 0)
    {
        return;
    }
    source_descriptor->trigger_count += event_count;
    source_descriptor->last_seen_time_ms = seen_time_ms;
}

/**
 * @brief 记录命令事件指标和观测日志。
 * @param controller_scheduler 调度器实例。
 * @param command_line 命令行文本。
 */
void controller_scheduler_note_command_event(controller_scheduler_t *controller_scheduler, const char *command_line)
{
    char log_line[160];
    unsigned long seen_time_ms;

    if (controller_scheduler == 0 || command_line == 0)
    {
        return;
    }

    seen_time_ms = controller_scheduler->runtime_port.current_time_ms(controller_scheduler->runtime_port.context);
    controller_scheduler->metrics.command_event_count += 1ul;
    controller_scheduler_note_source_event(&controller_scheduler->command_source, 1ul, seen_time_ms);
    snprintf(log_line, sizeof(log_line), "scheduler command line=%s", command_line);
    controller_scheduler_log_message(controller_scheduler, log_line);
}

/**
 * @brief 将初始化态调度器推进到运行态。
 * @param controller_scheduler 调度器实例。
 */
static void controller_scheduler_ensure_running_state(controller_scheduler_t *controller_scheduler)
{
    if (controller_scheduler != 0 &&
        controller_scheduler->runtime_state == CONTROLLER_SCHEDULER_RUNTIME_STATE_INITIALIZED)
    {
        controller_scheduler->runtime_state = CONTROLLER_SCHEDULER_RUNTIME_STATE_RUNNING;
    }
}

/**
 * @brief 在限定次数内执行主循环 tick，并更新周期指标。
 * @param controller_scheduler 调度器实例。
 * @param advance_time 是否推进逻辑时间。
 * @param elapsed_ms 本次推进的时间跨度。
 * @param count_cycle 是否累计周期计数。
 * @param cycle_count_increment 周期计数增量。
 * @return 执行成功返回 `ok`，主循环失败时返回对应错误。
 */
operation_result_t controller_scheduler_execute_bounded_ticks(controller_scheduler_t *controller_scheduler,
                                                             bool advance_time, unsigned long elapsed_ms,
                                                             bool count_cycle,
                                                             unsigned long cycle_count_increment)
{
    char log_line[160];
    operation_result_t result;
    unsigned int remaining_runs;
    unsigned long cycle_start_ms;

    if (controller_scheduler == 0)
    {
        return operation_result_fail(ERROR_CODE_INVALID_ARGUMENT);
    }

    cycle_start_ms = controller_scheduler_elapsed_ms(controller_scheduler);
    controller_scheduler->last_cycle_start_ms = cycle_start_ms;
    if (advance_time)
    {
        controller_scheduler->runtime_port.advance_time(controller_scheduler->runtime_port.context, elapsed_ms);
    }

    remaining_runs = controller_scheduler->config.max_triggers_per_tick > 0u
                         ? controller_scheduler->config.max_triggers_per_tick
                         : 1u;
    result = operation_result_ok();
    while (remaining_runs > 0u)
    {
        if (controller_scheduler->failpoint_control_tick_run)
        {
            controller_scheduler_record_error(controller_scheduler, "control_tick_run_failed", true);
            return operation_result_fail(ERROR_CODE_IO_FAILED);
        }
        result = controller_scheduler->runtime_port.run_control_tick(controller_scheduler->runtime_port.context);
        if (!result.ok)
        {
            controller_scheduler_record_error(controller_scheduler, "control_tick_run_failed", true);
            return result;
        }
        remaining_runs -= 1u;
        if (remaining_runs == 0u ||
            controller_scheduler->runtime_port.pending_trigger_count(controller_scheduler->runtime_port.context) == 0u)
        {
            break;
        }
    }

    controller_scheduler->last_cycle_duration_ms =
        controller_scheduler->forced_cycle_duration_ms > 0ul
            ? controller_scheduler->forced_cycle_duration_ms
            : controller_scheduler_elapsed_ms(controller_scheduler) - cycle_start_ms;
    controller_scheduler->forced_cycle_duration_ms = 0ul;
    if (count_cycle)
    {
        controller_scheduler->metrics.cycle_count += cycle_count_increment;
    }
    if ((advance_time && elapsed_ms > controller_scheduler->config.control_period_ms) ||
        controller_scheduler->last_cycle_duration_ms > controller_scheduler->config.overrun_warning_threshold_ms)
    {
        controller_scheduler->metrics.overrun_count += 1ul;
        controller_scheduler->metrics.consecutive_overrun_count += 1ul;
    }
    else
    {
        controller_scheduler->metrics.consecutive_overrun_count = 0ul;
    }
    controller_scheduler_update_pending_metric(controller_scheduler);

    snprintf(log_line, sizeof(log_line), "scheduler cycle=%lu duration_ms=%lu pending=%u state=%d",
             controller_scheduler->metrics.cycle_count, controller_scheduler->last_cycle_duration_ms,
             controller_scheduler->metrics.pending_trigger_count, (int)controller_scheduler->runtime_state);
    controller_scheduler_log_message(controller_scheduler, log_line);
    return result;
}

/**
 * @brief 消费待处理通知事件并更新通知快照。
 * @param controller_scheduler 调度器实例。
 * @return 处理成功返回 `ok`，参数非法时返回失败。
 */
static operation_result_t controller_scheduler_handle_notification(controller_scheduler_t *controller_scheduler)
{
    char log_line[160];
    unsigned int notification_count;
    unsigned long seen_time_ms;

    if (controller_scheduler == 0)
    {
        return operation_result_fail(ERROR_CODE_INVALID_ARGUMENT);
    }

    notification_count = controller_scheduler->pending_notification_count;
    controller_scheduler->pending_notification_count = 0u;
    if (notification_count == 0u)
    {
        return operation_result_ok();
    }

    seen_time_ms = controller_scheduler->runtime_port.current_time_ms(controller_scheduler->runtime_port.context);
    controller_scheduler->metrics.notification_event_count += notification_count;
    controller_scheduler->notification_snapshot.snapshot_version += notification_count;
    controller_scheduler->notification_snapshot.captured_time_ms = seen_time_ms;
    controller_scheduler->notification_snapshot.dirty = true;
    controller_scheduler_note_source_event(&controller_scheduler->notification_source, notification_count,
                                           seen_time_ms);
    snprintf(log_line, sizeof(log_line), "scheduler notification count=%u version=%lu", notification_count,
             controller_scheduler->notification_snapshot.snapshot_version);
    controller_scheduler_log_message(controller_scheduler, log_line);
    return operation_result_ok();
}

/**
 * @brief 处理退出事件并决定停止或进入排干态。
 * @param controller_scheduler 调度器实例。
 * @return 处理成功返回 `ok`，参数非法时返回失败。
 */
static operation_result_t controller_scheduler_handle_exit(controller_scheduler_t *controller_scheduler)
{
    char log_line[128];
    unsigned long seen_time_ms;

    if (controller_scheduler == 0)
    {
        return operation_result_fail(ERROR_CODE_INVALID_ARGUMENT);
    }

    controller_scheduler->pending_exit_event = false;
    seen_time_ms = controller_scheduler->runtime_port.current_time_ms(controller_scheduler->runtime_port.context);
    controller_scheduler->metrics.exit_event_count += 1ul;
    controller_scheduler_note_source_event(&controller_scheduler->exit_source, 1ul, seen_time_ms);

    if (controller_scheduler->exit_immediate ||
        controller_scheduler->config.exit_mode == CONTROLLER_SCHEDULER_EXIT_MODE_DIRECT ||
        controller_scheduler->config.bounded_drain_ticks == 0u)
    {
        controller_scheduler->runtime_state = CONTROLLER_SCHEDULER_RUNTIME_STATE_STOPPED;
    }
    else
    {
        controller_scheduler->runtime_state = CONTROLLER_SCHEDULER_RUNTIME_STATE_DRAINING;
        controller_scheduler->drain_ticks_remaining = controller_scheduler->config.bounded_drain_ticks;
    }

    snprintf(log_line, sizeof(log_line), "scheduler exit state=%d drain_ticks=%u",
             (int)controller_scheduler->runtime_state, controller_scheduler->drain_ticks_remaining);
    controller_scheduler_log_message(controller_scheduler, log_line);
    return operation_result_ok();
}

/**
 * @brief 设置内部退出请求，并按需唤醒 epoll 循环。
 * @param controller_scheduler 调度器实例。
 * @param immediate 是否立即退出。
 * @param signal_wakeup 是否向唤醒 fd 写入通知。
 * @return 请求成功返回 `ok`，写唤醒失败时返回对应错误。
 */
operation_result_t controller_scheduler_request_exit_internal(controller_scheduler_t *controller_scheduler,
                                                             bool immediate, bool signal_wakeup)
{
    uint64_t wakeup_value;
    ssize_t write_result;

    if (controller_scheduler == 0)
    {
        return operation_result_fail(ERROR_CODE_INVALID_ARGUMENT);
    }

    controller_scheduler->exit_requested = true;
    controller_scheduler->exit_immediate = immediate;
    controller_scheduler->pending_exit_event = true;
    if (!signal_wakeup)
    {
        return operation_result_ok();
    }
    if (controller_scheduler->failpoint_wakeup_write)
    {
        controller_scheduler_record_error(controller_scheduler, "wakeup_write_failed", true);
        return operation_result_fail(ERROR_CODE_IO_FAILED);
    }
    if (controller_scheduler->wakeup_fd < 0)
    {
        return operation_result_ok();
    }
    wakeup_value = 1u;
    write_result = write(controller_scheduler->wakeup_fd, &wakeup_value, sizeof(wakeup_value));
    if (write_result < 0 && errno != EAGAIN)
    {
        controller_scheduler_record_error(controller_scheduler, "wakeup_write_failed", true);
        return operation_result_fail(ERROR_CODE_IO_FAILED);
    }
    return operation_result_ok();
}

/**
 * @brief 在排干态下继续执行剩余运行时工作。
 * @param controller_scheduler 调度器实例。
 * @return 排干成功返回 `ok`，超时或执行失败时返回对应错误。
 */
static operation_result_t controller_scheduler_service_drain(controller_scheduler_t *controller_scheduler)
{
    operation_result_t result;

    if (controller_scheduler == 0)
    {
        return operation_result_fail(ERROR_CODE_INVALID_ARGUMENT);
    }
    if (controller_scheduler->runtime_state != CONTROLLER_SCHEDULER_RUNTIME_STATE_DRAINING)
    {
        return operation_result_ok();
    }
    if (!controller_scheduler->runtime_port.has_pending_work(controller_scheduler->runtime_port.context))
    {
        controller_scheduler->runtime_state = CONTROLLER_SCHEDULER_RUNTIME_STATE_STOPPED;
        return operation_result_ok();
    }
    if (controller_scheduler->drain_ticks_remaining == 0u)
    {
        controller_scheduler_record_error(controller_scheduler, "drain_limit_reached", true);
        return operation_result_fail(ERROR_CODE_TIMEOUT);
    }

    controller_scheduler->drain_ticks_remaining -= 1u;
    result = controller_scheduler_execute_bounded_ticks(controller_scheduler, true,
                                                        controller_scheduler->config.control_period_ms, false, 0ul);
    if (!result.ok)
    {
        return result;
    }
    if (!controller_scheduler->runtime_port.has_pending_work(controller_scheduler->runtime_port.context))
    {
        controller_scheduler->runtime_state = CONTROLLER_SCHEDULER_RUNTIME_STATE_STOPPED;
    }
    else if (controller_scheduler->drain_ticks_remaining == 0u)
    {
        controller_scheduler_record_error(controller_scheduler, "drain_limit_reached", true);
        return operation_result_fail(ERROR_CODE_TIMEOUT);
    }
    return operation_result_ok();
}

/**
 * @brief 按优先级分发当前已就绪的内部事件。
 * @param controller_scheduler 调度器实例。
 * @return 分发成功返回 `ok`，任一处理步骤失败时返回对应错误。
 */
static operation_result_t controller_scheduler_dispatch_ready_events(controller_scheduler_t *controller_scheduler)
{
    operation_result_t result;

    if (controller_scheduler == 0)
    {
        return operation_result_fail(ERROR_CODE_INVALID_ARGUMENT);
    }

    result = operation_result_ok();
    for (;;)
    {
        if (controller_scheduler->pending_exit_event)
        {
            result = controller_scheduler_handle_exit(controller_scheduler);
            if (!result.ok || controller_scheduler->runtime_state == CONTROLLER_SCHEDULER_RUNTIME_STATE_STOPPED)
            {
                return result;
            }
            continue;
        }
        if (controller_scheduler->runtime_state == CONTROLLER_SCHEDULER_RUNTIME_STATE_DRAINING)
        {
            return controller_scheduler_service_drain(controller_scheduler);
        }
        if (controller_scheduler->pending_command_event)
        {
            result = stdio_formal_command_adapter_handle_fd(&controller_scheduler->command_adapter,
                                                            controller_scheduler,
                                                            controller_scheduler->failpoint_command_read);
            if (!result.ok)
            {
                return result;
            }
            continue;
        }
        if (controller_scheduler->pending_notification_count > 0u)
        {
            result = controller_scheduler_handle_notification(controller_scheduler);
            if (!result.ok)
            {
                return result;
            }
            continue;
        }
        if (controller_scheduler->pending_period_expirations > 0ul)
        {
            unsigned long expirations = controller_scheduler->pending_period_expirations;

            controller_scheduler->pending_period_expirations = 0ul;
            return controller_scheduler_execute_bounded_ticks(
                controller_scheduler, true, controller_scheduler->config.control_period_ms * expirations, true, 1ul);
        }
        break;
    }
    return result;
}

/**
 * @brief 从 epoll 收集本轮已就绪事件并转成内部待处理标记。
 * @param controller_scheduler 调度器实例。
 * @return 收集成功返回 `ok`，底层 IO 失败时返回对应错误。
 */
static operation_result_t controller_scheduler_collect_ready_events(controller_scheduler_t *controller_scheduler)
{
    struct epoll_event events[CONTROLLER_SCHEDULER_MAX_EPOLL_EVENTS];
    int event_count;
    int event_index;

    if (controller_scheduler == 0)
    {
        return operation_result_fail(ERROR_CODE_INVALID_ARGUMENT);
    }

    event_count = epoll_wait(controller_scheduler->epoll_fd, events, CONTROLLER_SCHEDULER_MAX_EPOLL_EVENTS, -1);
    if (event_count < 0)
    {
        if (errno == EINTR)
        {
            return operation_result_ok();
        }
        controller_scheduler_record_error(controller_scheduler, "epoll_wait_failed", true);
        return operation_result_fail(ERROR_CODE_IO_FAILED);
    }

    for (event_index = 0; event_index < event_count; ++event_index)
    {
        if (events[event_index].data.fd == controller_scheduler->timer_fd)
        {
            uint64_t expirations;

            if (controller_scheduler->failpoint_timer_read)
            {
                controller_scheduler_record_error(controller_scheduler, "timer_read_failed", true);
                return operation_result_fail(ERROR_CODE_IO_FAILED);
            }
            if (read(controller_scheduler->timer_fd, &expirations, sizeof(expirations)) < 0)
            {
                controller_scheduler_record_error(controller_scheduler, "timer_read_failed", true);
                return operation_result_fail(ERROR_CODE_IO_FAILED);
            }
            controller_scheduler->pending_period_expirations += (unsigned long)expirations;
            continue;
        }
        if (events[event_index].data.fd == controller_scheduler->wakeup_fd)
        {
            uint64_t wakeup_count;

            if (controller_scheduler->failpoint_wakeup_read)
            {
                controller_scheduler_record_error(controller_scheduler, "wakeup_read_failed", true);
                return operation_result_fail(ERROR_CODE_IO_FAILED);
            }
            if (read(controller_scheduler->wakeup_fd, &wakeup_count, sizeof(wakeup_count)) < 0 && errno != EAGAIN)
            {
                controller_scheduler_record_error(controller_scheduler, "wakeup_read_failed", true);
                return operation_result_fail(ERROR_CODE_IO_FAILED);
            }
            continue;
        }
        if (stdio_formal_command_adapter_fd(&controller_scheduler->command_adapter) >= 0 &&
            events[event_index].data.fd == stdio_formal_command_adapter_fd(&controller_scheduler->command_adapter))
        {
            controller_scheduler->pending_command_event = true;
        }
    }
    return operation_result_ok();
}

/**
 * @brief 将指定 fd 注册到调度器的 epoll 实例。
 * @param controller_scheduler 调度器实例。
 * @param fd 待注册文件描述符。
 * @return 注册成功返回 `ok`，失败时返回 IO 错误。
 */
static operation_result_t controller_scheduler_register_epoll_fd(controller_scheduler_t *controller_scheduler, int fd)
{
    struct epoll_event event_registration;

    memset(&event_registration, 0, sizeof(event_registration));
    event_registration.events = EPOLLIN;
    event_registration.data.fd = fd;
    if (epoll_ctl(controller_scheduler->epoll_fd, EPOLL_CTL_ADD, fd, &event_registration) < 0)
    {
        controller_scheduler_record_error(controller_scheduler, "epoll_register_failed", true);
        return operation_result_fail(ERROR_CODE_IO_FAILED);
    }
    return operation_result_ok();
}

/**
 * @brief 校验调度器创建配置是否合法。
 * @param controller_scheduler_config 待校验配置。
 * @return 配置合法返回 `ok`，否则返回参数错误。
 */
static operation_result_t
controller_scheduler_validate_config(const controller_scheduler_config_t *controller_scheduler_config)
{
    if (controller_scheduler_config == 0)
    {
        return operation_result_fail(ERROR_CODE_INVALID_ARGUMENT);
    }
    if (controller_scheduler_config->control_period_ms == 0ul)
    {
        return operation_result_fail(ERROR_CODE_INVALID_ARGUMENT);
    }
    if (controller_scheduler_config->max_triggers_per_tick == 0u)
    {
        return operation_result_fail(ERROR_CODE_INVALID_ARGUMENT);
    }
    if (controller_scheduler_config->exit_mode == CONTROLLER_SCHEDULER_EXIT_MODE_BOUNDED_DRAIN &&
        controller_scheduler_config->bounded_drain_ticks == 0u)
    {
        return operation_result_fail(ERROR_CODE_INVALID_ARGUMENT);
    }
    return operation_result_ok();
}

static void controller_scheduler_linux_destroy_impl(controller_scheduler_t *controller_scheduler);

/**
 * @brief 创建 Linux 调度器内部实例。
 * @param runtime_port 调度器运行时端口，不能为空，各函数指针必须有效。
 * @param controller_scheduler_config 调度器配置。
 * @param controller_scheduler_stdio 可选标准输入输出绑定。
 * @return 创建成功返回调度器对象；失败时返回 `0`。
 */
static controller_scheduler_t *
controller_scheduler_linux_create_impl(const scheduler_runtime_port_t *runtime_port,
                                       const controller_scheduler_config_t *controller_scheduler_config,
                                       const controller_scheduler_stdio_t *controller_scheduler_stdio)
{
    controller_scheduler_t *controller_scheduler;
    struct itimerspec timer_spec;
    operation_result_t result;
    int command_fd;

    if (runtime_port == 0 || runtime_port->current_time_ms == 0 || runtime_port->run_control_tick == 0)
    {
        return 0;
    }
    result = controller_scheduler_validate_config(controller_scheduler_config);
    if (!result.ok)
    {
        return 0;
    }

    controller_scheduler = (controller_scheduler_t *)calloc(1u, sizeof(*controller_scheduler));
    if (controller_scheduler == 0)
    {
        return 0;
    }

    controller_scheduler->runtime_port = *runtime_port;

    controller_scheduler->config = *controller_scheduler_config;
    controller_scheduler->runtime_state = CONTROLLER_SCHEDULER_RUNTIME_STATE_INITIALIZED;
    controller_scheduler->epoll_fd = -1;
    controller_scheduler->timer_fd = -1;
    controller_scheduler->wakeup_fd = -1;
    stdio_formal_command_adapter_init(&controller_scheduler->command_adapter, controller_scheduler_stdio);
    controller_scheduler->monotonic_epoch_ms = monotonic_now_ms();
    controller_scheduler->command_source.source_kind = CONTROLLER_SCHEDULER_EVENT_SOURCE_COMMAND;
    controller_scheduler->notification_source.source_kind = CONTROLLER_SCHEDULER_EVENT_SOURCE_NOTIFICATION;
    controller_scheduler->exit_source.source_kind = CONTROLLER_SCHEDULER_EVENT_SOURCE_EXIT;
    controller_scheduler->epoll_fd = epoll_create1(EPOLL_CLOEXEC);
    controller_scheduler->timer_fd = timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK | TFD_CLOEXEC);
    controller_scheduler->wakeup_fd = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
    if (controller_scheduler->epoll_fd < 0 || controller_scheduler->timer_fd < 0 || controller_scheduler->wakeup_fd < 0)
    {
        controller_scheduler_linux_destroy_impl(controller_scheduler);
        return 0;
    }

    memset(&timer_spec, 0, sizeof(timer_spec));
    timer_spec.it_value.tv_sec = (time_t)(controller_scheduler_config->control_period_ms / 1000ul);
    timer_spec.it_value.tv_nsec = (long)((controller_scheduler_config->control_period_ms % 1000ul) * 1000000ul);
    timer_spec.it_interval = timer_spec.it_value;
    if (timerfd_settime(controller_scheduler->timer_fd, 0, &timer_spec, 0) < 0)
    {
        controller_scheduler_linux_destroy_impl(controller_scheduler);
        return 0;
    }
    if (!controller_scheduler_register_epoll_fd(controller_scheduler, controller_scheduler->timer_fd).ok ||
        !controller_scheduler_register_epoll_fd(controller_scheduler, controller_scheduler->wakeup_fd).ok)
    {
        controller_scheduler_linux_destroy_impl(controller_scheduler);
        return 0;
    }

    if (controller_scheduler_config->command_event_source_enabled)
    {
        command_fd = stdio_formal_command_adapter_enable(&controller_scheduler->command_adapter);
        if (command_fd >= 0)
        {
            if (!controller_scheduler_register_epoll_fd(controller_scheduler, command_fd).ok)
            {
                controller_scheduler_linux_destroy_impl(controller_scheduler);
                return 0;
            }
        }
        else if (stdio_formal_command_adapter_fd(&controller_scheduler->command_adapter) >= 0)
        {
            controller_scheduler_linux_destroy_impl(controller_scheduler);
            return 0;
        }
    }

    controller_scheduler_refresh_source_states(controller_scheduler);
    controller_scheduler_update_pending_metric(controller_scheduler);
    return controller_scheduler;
}

/**
 * @brief 销毁 Linux 调度器内部实例。
 * @param controller_scheduler 调度器对象；允许为 `0`。
 */
static void controller_scheduler_linux_destroy_impl(controller_scheduler_t *controller_scheduler)
{
    if (controller_scheduler == 0)
    {
        return;
    }

    stdio_formal_command_adapter_restore(&controller_scheduler->command_adapter);
    close_fd_if_needed(&controller_scheduler->timer_fd);
    close_fd_if_needed(&controller_scheduler->wakeup_fd);
    close_fd_if_needed(&controller_scheduler->epoll_fd);
    free(controller_scheduler);
}

controller_scheduler_t *controller_scheduler_create(const scheduler_runtime_port_t *runtime_port,
                                                    const controller_scheduler_config_t *controller_scheduler_config,
                                                    const controller_scheduler_stdio_t *controller_scheduler_stdio)
{
    return controller_scheduler_linux_create_impl(runtime_port, controller_scheduler_config,
                                                  controller_scheduler_stdio);
}

void controller_scheduler_destroy(controller_scheduler_t *controller_scheduler)
{
    controller_scheduler_linux_destroy_impl(controller_scheduler);
}

operation_result_t controller_scheduler_run(controller_scheduler_t *controller_scheduler)
{
    operation_result_t result;

    if (controller_scheduler == 0)
    {
        return operation_result_fail(ERROR_CODE_INVALID_ARGUMENT);
    }

    controller_scheduler_ensure_running_state(controller_scheduler);
    controller_scheduler->in_run_loop = true;
    while (controller_scheduler->runtime_state == CONTROLLER_SCHEDULER_RUNTIME_STATE_RUNNING ||
           controller_scheduler->runtime_state == CONTROLLER_SCHEDULER_RUNTIME_STATE_DRAINING)
    {
        result = controller_scheduler_collect_ready_events(controller_scheduler);
        if (!result.ok)
        {
            controller_scheduler->in_run_loop = false;
            return result;
        }
        result = controller_scheduler_dispatch_ready_events(controller_scheduler);
        if (!result.ok)
        {
            controller_scheduler->in_run_loop = false;
            return result;
        }
    }
    controller_scheduler->in_run_loop = false;
    return controller_scheduler->runtime_state == CONTROLLER_SCHEDULER_RUNTIME_STATE_FAILED
               ? operation_result_fail(ERROR_CODE_IO_FAILED)
               : operation_result_ok();
}

operation_result_t controller_scheduler_request_stop(controller_scheduler_t *controller_scheduler)
{
    return controller_scheduler_request_exit_internal(controller_scheduler, false, true);
}

operation_result_t controller_scheduler_read_view(const controller_scheduler_t *controller_scheduler,
                                                  controller_scheduler_state_view_t *state_view)
{
    if (controller_scheduler == 0 || state_view == 0)
    {
        return operation_result_fail(ERROR_CODE_INVALID_ARGUMENT);
    }

    memset(state_view, 0, sizeof(*state_view));
    state_view->runtime_state = controller_scheduler->runtime_state;
    state_view->control_period_ms = controller_scheduler->config.control_period_ms;
    state_view->last_cycle_start_ms = controller_scheduler->last_cycle_start_ms;
    state_view->last_cycle_duration_ms = controller_scheduler->last_cycle_duration_ms;
    state_view->command_source_state = controller_scheduler->command_source.source_state;
    state_view->notification_source_state = controller_scheduler->notification_source.source_state;
    state_view->exit_source_state = controller_scheduler->exit_source.source_state;
    state_view->metrics = controller_scheduler->metrics;
    return operation_result_ok();
}


operation_result_t controller_scheduler_linux_test_inject_period(controller_scheduler_t *controller_scheduler,
                                                                 unsigned int expiration_count)
{
    if (controller_scheduler == 0 || expiration_count == 0u)
    {
        return operation_result_fail(ERROR_CODE_INVALID_ARGUMENT);
    }
    controller_scheduler_ensure_running_state(controller_scheduler);
    controller_scheduler->pending_period_expirations += expiration_count;
    return controller_scheduler_dispatch_ready_events(controller_scheduler);
}

operation_result_t controller_scheduler_linux_test_inject_command(controller_scheduler_t *controller_scheduler,
                                                                  const char *command_line, char *response_line,
                                                                  size_t response_line_size)
{
    if (controller_scheduler == 0 || command_line == 0)
    {
        return operation_result_fail(ERROR_CODE_INVALID_ARGUMENT);
    }
    controller_scheduler_ensure_running_state(controller_scheduler);
    return stdio_formal_command_adapter_handle_command_event(&controller_scheduler->command_adapter,
                                                             controller_scheduler, command_line, response_line,
                                                             response_line_size, false);
}

operation_result_t controller_scheduler_linux_test_inject_notification(controller_scheduler_t *controller_scheduler,
                                                                       unsigned int notification_count)
{
    if (controller_scheduler == 0 || notification_count == 0u)
    {
        return operation_result_fail(ERROR_CODE_INVALID_ARGUMENT);
    }
    controller_scheduler_ensure_running_state(controller_scheduler);
    controller_scheduler->pending_notification_count += notification_count;
    return controller_scheduler_dispatch_ready_events(controller_scheduler);
}

operation_result_t controller_scheduler_linux_test_inject_exit(controller_scheduler_t *controller_scheduler,
                                                               bool immediate)
{
    operation_result_t result;

    if (controller_scheduler == 0)
    {
        return operation_result_fail(ERROR_CODE_INVALID_ARGUMENT);
    }
    controller_scheduler_ensure_running_state(controller_scheduler);
    result = controller_scheduler_request_exit_internal(controller_scheduler, immediate, false);
    if (!result.ok)
    {
        return result;
    }
    return controller_scheduler_dispatch_ready_events(controller_scheduler);
}

operation_result_t controller_scheduler_linux_test_step(controller_scheduler_t *controller_scheduler)
{
    if (controller_scheduler == 0)
    {
        return operation_result_fail(ERROR_CODE_INVALID_ARGUMENT);
    }
    controller_scheduler_ensure_running_state(controller_scheduler);
    return controller_scheduler_dispatch_ready_events(controller_scheduler);
}

operation_result_t controller_scheduler_linux_test_poll_once(controller_scheduler_t *controller_scheduler)
{
    operation_result_t result;

    if (controller_scheduler == 0)
    {
        return operation_result_fail(ERROR_CODE_INVALID_ARGUMENT);
    }
    controller_scheduler_ensure_running_state(controller_scheduler);
    result = controller_scheduler_collect_ready_events(controller_scheduler);
    if (!result.ok)
    {
        return result;
    }
    return controller_scheduler_dispatch_ready_events(controller_scheduler);
}

void controller_scheduler_linux_test_set_failpoint(
    controller_scheduler_t *controller_scheduler,
    controller_scheduler_linux_test_failpoint_t controller_scheduler_linux_test_failpoint, bool enabled)
{
    if (controller_scheduler == 0)
    {
        return;
    }

    switch (controller_scheduler_linux_test_failpoint)
    {
    case CONTROLLER_SCHEDULER_LINUX_TEST_FAIL_TIMER_READ:
        controller_scheduler->failpoint_timer_read = enabled;
        break;
    case CONTROLLER_SCHEDULER_LINUX_TEST_FAIL_WAKEUP_READ:
        controller_scheduler->failpoint_wakeup_read = enabled;
        break;
    case CONTROLLER_SCHEDULER_LINUX_TEST_FAIL_WAKEUP_WRITE:
        controller_scheduler->failpoint_wakeup_write = enabled;
        break;
    case CONTROLLER_SCHEDULER_LINUX_TEST_FAIL_COMMAND_READ:
        controller_scheduler->failpoint_command_read = enabled;
        break;
    case CONTROLLER_SCHEDULER_LINUX_TEST_FAIL_CONTROL_TICK_RUN:
        controller_scheduler->failpoint_control_tick_run = enabled;
        break;
    case CONTROLLER_SCHEDULER_LINUX_TEST_FAIL_NONE:
    default:
        break;
    }
}

void controller_scheduler_linux_test_set_cycle_duration(controller_scheduler_t *controller_scheduler,
                                                        unsigned long cycle_duration_ms)
{
    if (controller_scheduler == 0)
    {
        return;
    }
    controller_scheduler->forced_cycle_duration_ms = cycle_duration_ms;
}
