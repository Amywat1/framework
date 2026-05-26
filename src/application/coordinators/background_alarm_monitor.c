#include "application/coordinators/background_alarm_monitor.h"

#include <stdlib.h>
#include <string.h>

#include "application/services/alarm_evaluator.h"
#include "platform/worker_thread.h"
#include "src/application/jobs/alarm_detect_job.h"

typedef struct background_alarm_snapshot_mailbox_t
{
    try_lock_t *access_guard;
    unsigned long published_sequence;
    sensor_snapshot_t sensor_snapshot;
    unsigned long occurred_at_ms;
} background_alarm_snapshot_mailbox_t;

struct background_alarm_monitor_t
{
    background_alarm_settings_t settings;
    device_runtime_t device_runtime;
    const sensor_port_t *sensor_port;
    alarm_evaluator_t alarm_evaluator;
    background_alarm_snapshot_mailbox_t snapshot_mailbox;
    worker_thread_t *io_worker_thread;
    worker_thread_t *detect_worker_thread;
};

/**
 * @brief 通过传感器端口读取一次后台报警采样快照。
 * @param sensor_port 传感器端口，不能为空，且必须提供 `read_snapshot`。
 * @param sensor_snapshot 输出采样快照，不能为空。
 * @return 读取成功返回 `operation_result_ok()`；端口非法或 IO 失败时返回失败结果。
 */
static operation_result_t background_alarm_monitor_read_snapshot(const sensor_port_t *sensor_port,
                                                                 sensor_snapshot_t *sensor_snapshot)
{
    if (sensor_port == 0 || sensor_snapshot == 0 || sensor_port->read_snapshot == 0)
    {
        return operation_result_fail(ERROR_CODE_INVALID_ARGUMENT);
    }
    if (sensor_port->read_snapshot(sensor_port->context, sensor_snapshot) != 0)
    {
        return operation_result_fail(ERROR_CODE_IO_FAILED);
    }
    return operation_result_ok();
}

/**
 * @brief 初始化后台报警快照邮箱。
 * @param snapshot_mailbox 单生产者单消费者快照邮箱。
 * @return 初始化成功返回 `true`；内存不足时返回 `false`。
 */
static bool background_alarm_snapshot_mailbox_init(background_alarm_snapshot_mailbox_t *snapshot_mailbox)
{
    if (snapshot_mailbox == 0)
    {
        return false;
    }
    memset(&snapshot_mailbox->sensor_snapshot, 0, sizeof(snapshot_mailbox->sensor_snapshot));
    snapshot_mailbox->occurred_at_ms = 0ul;
    snapshot_mailbox->published_sequence = 0ul;
    snapshot_mailbox->access_guard = try_lock_create();
    return snapshot_mailbox->access_guard != 0;
}


/**
 * @brief 发布最新传感器快照到邮箱。
 * @param snapshot_mailbox 单生产者单消费者快照邮箱。
 * @param sensor_snapshot 本次读取到的传感器快照。
 * @param occurred_at_ms 本次快照采样时间。
 * @details 邮箱采用单槽最新值语义；若消费者正持锁，本次快照会被丢弃，由后续采样覆盖。
 */
static bool background_alarm_snapshot_mailbox_publish(background_alarm_snapshot_mailbox_t *snapshot_mailbox,
                                                      const sensor_snapshot_t *sensor_snapshot,
                                                      unsigned long occurred_at_ms)
{
    if (snapshot_mailbox == 0 || sensor_snapshot == 0)
    {
        return false;
    }
    if (!try_lock_acquire(snapshot_mailbox->access_guard))
    {
        return false;
    }

    snapshot_mailbox->sensor_snapshot = *sensor_snapshot;
    snapshot_mailbox->occurred_at_ms = occurred_at_ms;
    snapshot_mailbox->published_sequence += 1ul;
    try_lock_release(snapshot_mailbox->access_guard);
    return true;
}

/**
 * @brief 尝试读取一份稳定发布且尚未消费的新快照。
 * @param snapshot_mailbox 单生产者单消费者快照邮箱。
 * @param last_consumed_sequence 调用方维护的最近已消费序号。
 * @param sensor_snapshot 输出快照。
 * @param occurred_at_ms 输出采样时间。
 * @return 读到新快照时返回 `true`，否则返回 `false`。
 */
static bool background_alarm_snapshot_mailbox_try_read(background_alarm_snapshot_mailbox_t *snapshot_mailbox,
                                                       unsigned long *last_consumed_sequence,
                                                       sensor_snapshot_t *sensor_snapshot,
                                                       unsigned long *occurred_at_ms)
{
    unsigned long consumed_sequence;
    unsigned long published_sequence;

    if (snapshot_mailbox == 0 || last_consumed_sequence == 0 || sensor_snapshot == 0 || occurred_at_ms == 0)
    {
        return false;
    }
    if (!try_lock_acquire(snapshot_mailbox->access_guard))
    {
        return false;
    }

    consumed_sequence = *last_consumed_sequence;
    published_sequence = snapshot_mailbox->published_sequence;
    if (published_sequence == 0ul || published_sequence == consumed_sequence)
    {
        try_lock_release(snapshot_mailbox->access_guard);
        return false;
    }

    *sensor_snapshot = snapshot_mailbox->sensor_snapshot;
    *occurred_at_ms = snapshot_mailbox->occurred_at_ms;
    *last_consumed_sequence = published_sequence;
    try_lock_release(snapshot_mailbox->access_guard);
    return true;
}

/**
 * @brief 后台报警 IO 线程入口，仅负责周期读取并发布最新快照。
 * @param run_ctx 运行控制句柄。
 * @param context 监控实例。
 */
static void background_alarm_monitor_io_entry(worker_run_ctx_t *run_ctx, void *context)
{
    background_alarm_monitor_t *monitor;
    sensor_snapshot_t sensor_snapshot;
    operation_result_t result;

    monitor = (background_alarm_monitor_t *)context;
    if (monitor == 0)
    {
        return;
    }

    while (!worker_run_ctx_stop_requested(run_ctx))
    {
        result = background_alarm_monitor_read_snapshot(monitor->sensor_port, &sensor_snapshot);
        if (result.ok &&
            background_alarm_snapshot_mailbox_publish(&monitor->snapshot_mailbox, &sensor_snapshot,
                                                      worker_run_ctx_current_time_ms(run_ctx)))
        {
            (void)worker_thread_notify(monitor->detect_worker_thread);
        }

        (void)worker_run_ctx_wait(run_ctx, monitor->settings.io_sample_period_ms);
    }
}

/**
 * @brief 后台报警检测线程入口，只消费新快照并执行告警检测。
 * @param run_ctx 运行控制句柄。
 * @param context 监控实例。
 */
static void background_alarm_monitor_detect_entry(worker_run_ctx_t *run_ctx, void *context)
{
    unsigned long last_consumed_sequence;
    background_alarm_monitor_t *monitor;
    sensor_snapshot_t sensor_snapshot;
    unsigned long occurred_at_ms;

    monitor = (background_alarm_monitor_t *)context;
    if (monitor == 0)
    {
        return;
    }

    last_consumed_sequence = 0ul;
    while (!worker_run_ctx_stop_requested(run_ctx))
    {
        if (background_alarm_snapshot_mailbox_try_read(&monitor->snapshot_mailbox, &last_consumed_sequence,
                                                       &sensor_snapshot, &occurred_at_ms))
        {
            (void)alarm_detect_job_process_snapshot(monitor->device_runtime, &monitor->alarm_evaluator,
                                                    &sensor_snapshot, occurred_at_ms);
            continue;
        }

        (void)worker_run_ctx_wait(run_ctx, monitor->settings.detect_period_ms);
    }
}

operation_result_t background_alarm_monitor_create(background_alarm_monitor_t **monitor,
                                                   const background_alarm_monitor_config_t *config)
{
    background_alarm_monitor_t *created_monitor;

    if (monitor == 0 || config == 0)
    {
        return operation_result_fail(ERROR_CODE_INVALID_ARGUMENT);
    }
    *monitor = 0;
    if (config->settings.enabled &&
        (config->device_runtime == 0 || config->sensor_port == 0 || config->sensor_port->read_snapshot == 0 ||
         config->settings.io_sample_period_ms == 0ul || config->settings.detect_period_ms == 0ul))
    {
        return operation_result_fail(ERROR_CODE_INVALID_ARGUMENT);
    }

    created_monitor = (background_alarm_monitor_t *)calloc(1u, sizeof(*created_monitor));
    if (created_monitor == 0)
    {
        return operation_result_fail(ERROR_CODE_RESOURCE_UNAVAILABLE);
    }

    created_monitor->settings = config->settings;
    created_monitor->device_runtime = config->device_runtime;
    created_monitor->sensor_port = config->sensor_port;
    alarm_evaluator_init(&created_monitor->alarm_evaluator);
    if (!background_alarm_snapshot_mailbox_init(&created_monitor->snapshot_mailbox))
    {
        free(created_monitor);
        return operation_result_fail(ERROR_CODE_RESOURCE_UNAVAILABLE);
    }
    *monitor = created_monitor;
    return operation_result_ok();
}

operation_result_t background_alarm_monitor_start(background_alarm_monitor_t *monitor)
{
    operation_result_t result;
    worker_thread_config_t worker_thread_config;

    if (monitor == 0 || !monitor->settings.enabled)
    {
        return operation_result_ok();
    }
    if (monitor->io_worker_thread != 0 || monitor->detect_worker_thread != 0)
    {
        return operation_result_ok();
    }

    memset(&worker_thread_config, 0, sizeof(worker_thread_config));
    worker_thread_config.entry = background_alarm_monitor_detect_entry;
    worker_thread_config.context = monitor;
    result = worker_thread_start(&monitor->detect_worker_thread, &worker_thread_config);
    if (!result.ok)
    {
        return result;
    }

    worker_thread_config.entry = background_alarm_monitor_io_entry;
    result = worker_thread_start(&monitor->io_worker_thread, &worker_thread_config);
    if (!result.ok)
    {
        background_alarm_monitor_stop(monitor);
        return result;
    }
    return operation_result_ok();
}

void background_alarm_monitor_stop(background_alarm_monitor_t *monitor)
{
    if (monitor == 0)
    {
        return;
    }
    if (monitor->detect_worker_thread != 0)
    {
        (void)worker_thread_request_stop(monitor->detect_worker_thread);
    }
    if (monitor->io_worker_thread != 0)
    {
        (void)worker_thread_request_stop(monitor->io_worker_thread);
    }
    if (monitor->io_worker_thread != 0)
    {
        (void)worker_thread_join(monitor->io_worker_thread);
    }
    if (monitor->detect_worker_thread != 0)
    {
        (void)worker_thread_join(monitor->detect_worker_thread);
    }
    if (monitor->io_worker_thread != 0)
    {
        worker_thread_destroy(monitor->io_worker_thread);
        monitor->io_worker_thread = 0;
    }
    if (monitor->detect_worker_thread != 0)
    {
        worker_thread_destroy(monitor->detect_worker_thread);
        monitor->detect_worker_thread = 0;
    }
}

void background_alarm_monitor_destroy(background_alarm_monitor_t *monitor)
{
    if (monitor == 0)
    {
        return;
    }

    background_alarm_monitor_stop(monitor);
    try_lock_destroy(monitor->snapshot_mailbox.access_guard);
    free(monitor);
}
