#define _POSIX_C_SOURCE 200809L

#include "platform/background_alarm_monitor.h"

#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "application/services/alarm_evaluator.h"
#include "platform/worker_thread.h"
#include "src/application/jobs/alarm_detect_job.h"

typedef struct background_alarm_snapshot_mailbox_t
{
    atomic_bool access_guard;
    unsigned long published_sequence;
    sensor_snapshot_t sensor_snapshot;
    unsigned long occurred_at_ms;
} background_alarm_snapshot_mailbox_t;

struct background_alarm_monitor_t
{
    background_alarm_settings_t settings;
    system_context_t system_context;
    const sensor_port_t *sensor_port;
    alarm_evaluator_t alarm_evaluator;
    background_alarm_snapshot_mailbox_t snapshot_mailbox;
    worker_thread_t *io_worker_thread;
    worker_thread_t *detect_worker_thread;
};

/**
 * @brief 将毫秒时长转换为 `timespec`。
 * @param sleep_ms 休眠毫秒数。
 * @param sleep_time 输出 `timespec`。
 */
static void background_alarm_monitor_build_sleep_time(unsigned long sleep_ms, struct timespec *sleep_time)
{
    if (sleep_time == 0)
    {
        return;
    }

    sleep_time->tv_sec = (time_t)(sleep_ms / 1000ul);
    sleep_time->tv_nsec = (long)((sleep_ms % 1000ul) * 1000000ul);
}

/**
 * @brief 读取当前单调时钟的毫秒值。
 * @return 读取成功时返回毫秒值，失败时返回 `0`。
 */
static unsigned long background_alarm_monitor_read_monotonic_ms(void)
{
    struct timespec current_time;

    if (clock_gettime(CLOCK_MONOTONIC, &current_time) != 0)
    {
        return 0ul;
    }

    return (unsigned long)(current_time.tv_sec * 1000ul) + (unsigned long)(current_time.tv_nsec / 1000000ul);
}

/**
 * @brief 按短切片休眠，避免后台线程在停止时长时间阻塞。
 * @param worker_thread 后台线程句柄。
 * @param sleep_ms 总休眠时长。
 */
static void background_alarm_monitor_sleep_with_stop_check(worker_thread_t *worker_thread, unsigned long sleep_ms)
{
    static const unsigned long s_sleep_slice_ms = 20ul;
    struct timespec sleep_time;
    unsigned long remaining_ms;
    unsigned long current_slice_ms;

    remaining_ms = sleep_ms;
    while (remaining_ms > 0ul && !worker_thread_stop_requested(worker_thread))
    {
        current_slice_ms = remaining_ms > s_sleep_slice_ms ? s_sleep_slice_ms : remaining_ms;
        background_alarm_monitor_build_sleep_time(current_slice_ms, &sleep_time);
        (void)nanosleep(&sleep_time, 0);
        remaining_ms -= current_slice_ms;
    }
}

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
 */
static void background_alarm_snapshot_mailbox_init(background_alarm_snapshot_mailbox_t *snapshot_mailbox)
{
    if (snapshot_mailbox == 0)
    {
        return;
    }

    memset(&snapshot_mailbox->sensor_snapshot, 0, sizeof(snapshot_mailbox->sensor_snapshot));
    snapshot_mailbox->occurred_at_ms = 0ul;
    snapshot_mailbox->published_sequence = 0ul;
    atomic_init(&snapshot_mailbox->access_guard, false);
}

/**
 * @brief 尝试获取后台报警邮箱访问锁。
 * @param snapshot_mailbox 单生产者单消费者快照邮箱。
 * @return 获取成功返回 `true`，否则返回 `false`。
 */
static bool background_alarm_snapshot_mailbox_try_lock(background_alarm_snapshot_mailbox_t *snapshot_mailbox)
{
    bool expected_unlocked;

    if (snapshot_mailbox == 0)
    {
        return false;
    }

    expected_unlocked = false;
    return atomic_compare_exchange_strong_explicit(&snapshot_mailbox->access_guard, &expected_unlocked, true,
                                                   memory_order_acquire, memory_order_relaxed);
}

/**
 * @brief 释放后台报警邮箱访问锁。
 * @param snapshot_mailbox 单生产者单消费者快照邮箱。
 */
static void background_alarm_snapshot_mailbox_unlock(background_alarm_snapshot_mailbox_t *snapshot_mailbox)
{
    if (snapshot_mailbox == 0)
    {
        return;
    }

    atomic_store_explicit(&snapshot_mailbox->access_guard, false, memory_order_release);
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
    if (!background_alarm_snapshot_mailbox_try_lock(snapshot_mailbox))
    {
        return false;
    }

    snapshot_mailbox->sensor_snapshot = *sensor_snapshot;
    snapshot_mailbox->occurred_at_ms = occurred_at_ms;
    snapshot_mailbox->published_sequence += 1ul;
    background_alarm_snapshot_mailbox_unlock(snapshot_mailbox);
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
    if (!background_alarm_snapshot_mailbox_try_lock(snapshot_mailbox))
    {
        return false;
    }

    consumed_sequence = *last_consumed_sequence;
    published_sequence = snapshot_mailbox->published_sequence;
    if (published_sequence == 0ul || published_sequence == consumed_sequence)
    {
        background_alarm_snapshot_mailbox_unlock(snapshot_mailbox);
        return false;
    }

    *sensor_snapshot = snapshot_mailbox->sensor_snapshot;
    *occurred_at_ms = snapshot_mailbox->occurred_at_ms;
    *last_consumed_sequence = published_sequence;
    background_alarm_snapshot_mailbox_unlock(snapshot_mailbox);
    return true;
}

/**
 * @brief 后台报警 IO 线程入口，仅负责周期读取并发布最新快照。
 * @param worker_thread 后台线程句柄。
 * @param context 监控实例。
 */
static void background_alarm_monitor_io_entry(worker_thread_t *worker_thread, void *context)
{
    background_alarm_monitor_t *monitor;
    sensor_snapshot_t sensor_snapshot;
    operation_result_t result;

    monitor = (background_alarm_monitor_t *)context;
    if (monitor == 0)
    {
        return;
    }

    while (!worker_thread_stop_requested(worker_thread))
    {
        result = background_alarm_monitor_read_snapshot(monitor->sensor_port, &sensor_snapshot);
        if (result.ok &&
            background_alarm_snapshot_mailbox_publish(&monitor->snapshot_mailbox, &sensor_snapshot,
                                                      background_alarm_monitor_read_monotonic_ms()))
        {
            (void)worker_thread_notify(monitor->detect_worker_thread);
        }

        background_alarm_monitor_sleep_with_stop_check(worker_thread, monitor->settings.io_sample_period_ms);
    }
}

/**
 * @brief 后台报警检测线程入口，只消费新快照并执行告警检测。
 * @param worker_thread 后台线程句柄。
 * @param context 监控实例。
 */
static void background_alarm_monitor_detect_entry(worker_thread_t *worker_thread, void *context)
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
    while (!worker_thread_stop_requested(worker_thread))
    {
        if (background_alarm_snapshot_mailbox_try_read(&monitor->snapshot_mailbox, &last_consumed_sequence,
                                                       &sensor_snapshot, &occurred_at_ms))
        {
            (void)alarm_detect_job_process_snapshot(monitor->system_context, &monitor->alarm_evaluator,
                                                    &sensor_snapshot, occurred_at_ms);
            continue;
        }

        (void)worker_thread_wait(worker_thread, monitor->settings.detect_period_ms);
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
        (config->system_context == 0 || config->sensor_port == 0 || config->sensor_port->read_snapshot == 0 ||
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
    created_monitor->system_context = config->system_context;
    created_monitor->sensor_port = config->sensor_port;
    alarm_evaluator_init(&created_monitor->alarm_evaluator);
    background_alarm_snapshot_mailbox_init(&created_monitor->snapshot_mailbox);
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
    free(monitor);
}
