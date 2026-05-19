#define _POSIX_C_SOURCE 200809L

#include "platform/worker_thread.h"

#include <errno.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <time.h>

struct worker_thread_t
{
    pthread_t thread;
    pthread_mutex_t mutex;
    pthread_cond_t condition;
    worker_thread_entry_t entry;
    void *context;
    atomic_bool stop_requested;
    unsigned long notification_count;
    bool started;
    bool joined;
};

/**
 * @brief pthread 入口壳子。
 * @param argument 指向 `worker_thread_t` 的指针。
 * @return 固定返回 `0`。
 */
static void *worker_thread_linux_entry(void *argument)
{
    worker_thread_t *worker_thread;

    worker_thread = (worker_thread_t *)argument;
    if (worker_thread == 0 || worker_thread->entry == 0)
    {
        return 0;
    }

    worker_thread->entry(worker_thread, worker_thread->context);
    return 0;
}

operation_result_t worker_thread_start(worker_thread_t **worker_thread,
                                       const worker_thread_config_t *worker_thread_config)
{
    worker_thread_t *created_thread;
    pthread_condattr_t condition_attr;
    int create_result;

    if (worker_thread == 0 || worker_thread_config == 0 || worker_thread_config->entry == 0)
    {
        return operation_result_fail(ERROR_CODE_INVALID_ARGUMENT);
    }

    *worker_thread = 0;
    created_thread = (worker_thread_t *)calloc(1u, sizeof(*created_thread));
    if (created_thread == 0)
    {
        return operation_result_fail(ERROR_CODE_RESOURCE_UNAVAILABLE);
    }

    created_thread->entry = worker_thread_config->entry;
    created_thread->context = worker_thread_config->context;
    atomic_init(&created_thread->stop_requested, false);
    if (pthread_mutex_init(&created_thread->mutex, 0) != 0)
    {
        free(created_thread);
        return operation_result_fail(ERROR_CODE_RESOURCE_UNAVAILABLE);
    }
    if (pthread_condattr_init(&condition_attr) != 0)
    {
        pthread_mutex_destroy(&created_thread->mutex);
        free(created_thread);
        return operation_result_fail(ERROR_CODE_RESOURCE_UNAVAILABLE);
    }
    if (pthread_condattr_setclock(&condition_attr, CLOCK_MONOTONIC) != 0)
    {
        (void)pthread_condattr_destroy(&condition_attr);
        pthread_mutex_destroy(&created_thread->mutex);
        free(created_thread);
        return operation_result_fail(ERROR_CODE_RESOURCE_UNAVAILABLE);
    }
    if (pthread_cond_init(&created_thread->condition, &condition_attr) != 0)
    {
        (void)pthread_condattr_destroy(&condition_attr);
        pthread_mutex_destroy(&created_thread->mutex);
        free(created_thread);
        return operation_result_fail(ERROR_CODE_RESOURCE_UNAVAILABLE);
    }
    (void)pthread_condattr_destroy(&condition_attr);

    create_result = pthread_create(&created_thread->thread, 0, worker_thread_linux_entry, created_thread);
    if (create_result != 0)
    {
        pthread_cond_destroy(&created_thread->condition);
        pthread_mutex_destroy(&created_thread->mutex);
        free(created_thread);
        return operation_result_fail(ERROR_CODE_RESOURCE_UNAVAILABLE);
    }

    created_thread->started = true;
    *worker_thread = created_thread;
    return operation_result_ok();
}

operation_result_t worker_thread_notify(worker_thread_t *worker_thread)
{
    int lock_result;
    int signal_result;

    if (worker_thread == 0)
    {
        return operation_result_ok();
    }

    lock_result = pthread_mutex_lock(&worker_thread->mutex);
    if (lock_result != 0)
    {
        return operation_result_fail(ERROR_CODE_IO_FAILED);
    }

    worker_thread->notification_count += 1ul;
    signal_result = pthread_cond_signal(&worker_thread->condition);
    (void)pthread_mutex_unlock(&worker_thread->mutex);
    return signal_result == 0 ? operation_result_ok() : operation_result_fail(ERROR_CODE_IO_FAILED);
}

bool worker_thread_wait(worker_thread_t *worker_thread, unsigned long timeout_ms)
{
    struct timespec deadline;
    int wait_result;
    int lock_result;

    if (worker_thread == 0 || worker_thread_stop_requested(worker_thread))
    {
        return false;
    }

    lock_result = pthread_mutex_lock(&worker_thread->mutex);
    if (lock_result != 0)
    {
        return false;
    }

    while (worker_thread->notification_count == 0ul && !atomic_load(&worker_thread->stop_requested))
    {
        if (timeout_ms == 0ul)
        {
            (void)pthread_mutex_unlock(&worker_thread->mutex);
            return false;
        }

        if (clock_gettime(CLOCK_MONOTONIC, &deadline) != 0)
        {
            (void)pthread_mutex_unlock(&worker_thread->mutex);
            return false;
        }
        deadline.tv_sec += (time_t)(timeout_ms / 1000ul);
        deadline.tv_nsec += (long)((timeout_ms % 1000ul) * 1000000ul);
        if (deadline.tv_nsec >= 1000000000l)
        {
            deadline.tv_sec += 1;
            deadline.tv_nsec -= 1000000000l;
        }

        wait_result = pthread_cond_timedwait(&worker_thread->condition, &worker_thread->mutex, &deadline);
        if (wait_result == ETIMEDOUT)
        {
            (void)pthread_mutex_unlock(&worker_thread->mutex);
            return false;
        }
        if (wait_result != 0)
        {
            (void)pthread_mutex_unlock(&worker_thread->mutex);
            return false;
        }
    }

    if (worker_thread->notification_count > 0ul)
    {
        worker_thread->notification_count -= 1ul;
        (void)pthread_mutex_unlock(&worker_thread->mutex);
        return true;
    }

    (void)pthread_mutex_unlock(&worker_thread->mutex);
    return false;
}

operation_result_t worker_thread_request_stop(worker_thread_t *worker_thread)
{
    int lock_result;
    int broadcast_result;

    if (worker_thread == 0)
    {
        return operation_result_ok();
    }

    atomic_store(&worker_thread->stop_requested, true);
    lock_result = pthread_mutex_lock(&worker_thread->mutex);
    if (lock_result != 0)
    {
        return operation_result_fail(ERROR_CODE_IO_FAILED);
    }
    broadcast_result = pthread_cond_broadcast(&worker_thread->condition);
    (void)pthread_mutex_unlock(&worker_thread->mutex);
    return broadcast_result == 0 ? operation_result_ok() : operation_result_fail(ERROR_CODE_IO_FAILED);
}

bool worker_thread_stop_requested(const worker_thread_t *worker_thread)
{
    if (worker_thread == 0)
    {
        return true;
    }

    return atomic_load(&worker_thread->stop_requested);
}

operation_result_t worker_thread_join(worker_thread_t *worker_thread)
{
    int join_result;

    if (worker_thread == 0 || !worker_thread->started || worker_thread->joined)
    {
        return operation_result_ok();
    }

    join_result = pthread_join(worker_thread->thread, 0);
    if (join_result != 0)
    {
        return operation_result_fail(ERROR_CODE_IO_FAILED);
    }

    worker_thread->joined = true;
    return operation_result_ok();
}

void worker_thread_destroy(worker_thread_t *worker_thread)
{
    if (worker_thread == 0)
    {
        return;
    }

    if (worker_thread->started && !worker_thread->joined)
    {
        (void)worker_thread_request_stop(worker_thread);
        (void)worker_thread_join(worker_thread);
    }

    pthread_cond_destroy(&worker_thread->condition);
    pthread_mutex_destroy(&worker_thread->mutex);
    free(worker_thread);
}
