/**
 * @file    hal_io_manager.c
 * @brief   通用 HAL I/O 单所有者事务管理器实现。
 */

#include "adapters/outbound/hal/components/io_manager/hal_io_manager.h"

#include "common/log.h"
#include "common/sw_mutex.h"
#include "common/time_util.h"

#include <errno.h>
#include <pthread.h>
#include <stdbool.h>
#include <string.h>
#include <time.h>

typedef enum {
    IO_MANAGER_SLOT_FREE = 0,
    IO_MANAGER_SLOT_QUEUED,
    IO_MANAGER_SLOT_RUNNING,
    IO_MANAGER_SLOT_DONE
} io_manager_slot_state_t;

typedef union {
    long double alignment;
    uint8_t     bytes[HAL_IO_MANAGER_PAYLOAD_MAX];
} io_manager_payload_t;

typedef struct {
    bool                      used;
    bool                      waiter_gone;
    io_manager_slot_state_t   state;
    hal_io_manager_priority_t priority;
    uint64_t                  deadline_ms;
    io_manager_payload_t      request;
    size_t                    request_size;
    io_manager_payload_t      response;
    size_t                    response_size;
    sw_err_t                  result;
} io_manager_slot_t;

static hal_io_manager_cfg_t s_cfg;
static io_manager_slot_t    s_slots[HAL_IO_MANAGER_QUEUE_CAPACITY];
static pthread_mutex_t      s_mutex;
static pthread_once_t       s_mutex_once = PTHREAD_ONCE_INIT;
static pthread_cond_t       s_wake       = PTHREAD_COND_INITIALIZER;
static pthread_t            s_worker;
static bool                 s_initialized;
static bool                 s_started;
static uint64_t             s_next_tick_ms;
#ifdef HAL_IO_MANAGER_UNIT_TEST
static bool s_stop_requested;
static bool s_worker_exited;
#endif

static void io_manager_mutex_init_once(void)
{
    (void)sw_mutex_init_prio_inherit(&s_mutex);
}

/** @brief 确保管理器互斥量已初始化（幂等，所有公开加锁入口调用）。 */
static void io_manager_mutex_ready(void)
{
    (void)pthread_once(&s_mutex_once, io_manager_mutex_init_once);
}

static int io_manager_find_slot_locked(void)
{
    unsigned int i;

    for (i = 0U; i < HAL_IO_MANAGER_QUEUE_CAPACITY; ++i) {
        if (!s_slots[i].used) {
            return (int)i;
        }
    }
    return -1;
}

static int io_manager_find_next_locked(void)
{
    int                       selected = -1;
    unsigned int              i;
    uint64_t                  selected_deadline = UINT64_MAX;
    hal_io_manager_priority_t selected_priority = HAL_IO_MANAGER_PRIORITY_LOW;

    for (i = 0U; i < HAL_IO_MANAGER_QUEUE_CAPACITY; ++i) {
        io_manager_slot_t *slot = &s_slots[i];

        if (!slot->used || (slot->state != IO_MANAGER_SLOT_QUEUED)) {
            continue;
        }
        if ((selected < 0) || (slot->priority > selected_priority)
            || ((slot->priority == selected_priority) && (slot->deadline_ms < selected_deadline))) {
            selected          = (int)i;
            selected_priority = slot->priority;
            selected_deadline = slot->deadline_ms;
        }
    }
    return selected;
}

static bool io_manager_is_worker_locked(void)
{
    return s_started && pthread_equal(pthread_self(), s_worker) != 0;
}

static void io_manager_release_slot_locked(io_manager_slot_t *slot)
{
    memset(slot, 0, sizeof(*slot));
    slot->state = IO_MANAGER_SLOT_FREE;
}

static void io_manager_execute_slot(int slot_index)
{
    io_manager_slot_t *slot = &s_slots[slot_index];
    sw_err_t           result;
    uint64_t           now_ms = time_util_get_ms();

    if (now_ms >= slot->deadline_ms) {
        result = SW_ERR_TIMEOUT;
    } else {
        result = s_cfg.execute(
            s_cfg.execute_ctx, slot->request.bytes, slot->request_size, slot->response.bytes, slot->response_size);
    }

    (void)pthread_mutex_lock(&s_mutex);
    slot->result = result;
    slot->state  = IO_MANAGER_SLOT_DONE;
    (void)pthread_cond_broadcast(&s_wake);
    if (slot->waiter_gone) {
        io_manager_release_slot_locked(slot);
    }
    (void)pthread_mutex_unlock(&s_mutex);
}

static void *io_manager_worker_fn(void *arg)
{
    (void)arg;
    s_next_tick_ms = time_util_get_ms();

    for (;;) {
        int             slot_index;
        bool            do_tick = false;
        struct timespec wait_deadline;
        uint64_t        now_ms;

        (void)pthread_mutex_lock(&s_mutex);
#ifdef HAL_IO_MANAGER_UNIT_TEST
        if (s_stop_requested) {
            s_worker_exited = true;
            (void)pthread_cond_broadcast(&s_wake);
            (void)pthread_mutex_unlock(&s_mutex);
            return NULL;
        }
#endif
        now_ms     = time_util_get_ms();
        slot_index = io_manager_find_next_locked();
        if ((s_cfg.tick != NULL) && (now_ms >= s_next_tick_ms)) {
            do_tick        = true;
            s_next_tick_ms = now_ms + s_cfg.tick_period_ms;
        }

        if ((slot_index < 0) && !do_tick) {
            uint32_t wait_ms = (s_next_tick_ms > now_ms) ? (uint32_t)(s_next_tick_ms - now_ms) : 1U;

            time_util_fill_deadline(wait_ms, &wait_deadline);
            (void)pthread_cond_timedwait(&s_wake, &s_mutex, &wait_deadline);
            (void)pthread_mutex_unlock(&s_mutex);
            continue;
        }

        if (slot_index >= 0) {
            s_slots[slot_index].state = IO_MANAGER_SLOT_RUNNING;
        }
        (void)pthread_mutex_unlock(&s_mutex);

        /* 同轮先响应一个已排队事务，再执行到期 tick，避免慢轮询饿死安全事务。 */
        if (slot_index >= 0) {
            io_manager_execute_slot(slot_index);
        }
        if (do_tick) {
            s_cfg.tick(s_cfg.tick_ctx);
        }
    }

    return NULL;
}

sw_err_t hal_io_manager_init(const hal_io_manager_cfg_t *cfg)
{
    if ((cfg == NULL) || (cfg->name == NULL) || (cfg->tick_period_ms == 0U) || (cfg->tick == NULL)
        || (cfg->execute == NULL)) {
        return SW_ERR_PARAM;
    }
    io_manager_mutex_ready();
    (void)pthread_mutex_lock(&s_mutex);
    if (s_started) {
        (void)pthread_mutex_unlock(&s_mutex);
        return SW_ERR_STATE;
    }
    memset(s_slots, 0, sizeof(s_slots));
    s_cfg          = *cfg;
    s_initialized  = true;
    s_next_tick_ms = 0U;
#ifdef HAL_IO_MANAGER_UNIT_TEST
    s_stop_requested = false;
    s_worker_exited  = false;
#endif
    (void)pthread_mutex_unlock(&s_mutex);
    return SW_OK;
}

sw_err_t hal_io_manager_start(void)
{
    int ret;

    io_manager_mutex_ready();
    (void)pthread_mutex_lock(&s_mutex);
    if (!s_initialized) {
        (void)pthread_mutex_unlock(&s_mutex);
        return SW_ERR_NOT_INIT;
    }
    if (s_started) {
        (void)pthread_mutex_unlock(&s_mutex);
        return SW_ERR_STATE;
    }
#ifdef HAL_IO_MANAGER_UNIT_TEST
    s_stop_requested = false;
    s_worker_exited  = false;
#endif
    ret = pthread_create(&s_worker, NULL, io_manager_worker_fn, NULL);
    if (ret != 0) {
        (void)pthread_mutex_unlock(&s_mutex);
        LOG_ERROR("hal_io_manager: worker create failed errno=%d", ret);
        return SW_ERR_HW;
    }
    s_started = true;
    (void)pthread_mutex_unlock(&s_mutex);
    LOG_INFO("hal_io_manager: worker started name=%s", s_cfg.name);
    return SW_OK;
}

sw_err_t hal_io_manager_call(hal_io_manager_priority_t priority,
                             uint32_t                  timeout_ms,
                             const void               *request,
                             size_t                    request_size,
                             void                     *response,
                             size_t                    response_size)
{
    io_manager_slot_t *slot;
    int                slot_index;
    struct timespec    wait_deadline;
    int                wait_ret;
    sw_err_t           result;

    if ((request_size > HAL_IO_MANAGER_PAYLOAD_MAX) || (response_size > HAL_IO_MANAGER_PAYLOAD_MAX)
        || ((request == NULL) && (request_size != 0U)) || ((response == NULL) && (response_size != 0U))
        || (timeout_ms == 0U) || (priority > HAL_IO_MANAGER_PRIORITY_CRITICAL)) {
        return SW_ERR_PARAM;
    }
    io_manager_mutex_ready();
    (void)pthread_mutex_lock(&s_mutex);
    if (!s_initialized) {
        (void)pthread_mutex_unlock(&s_mutex);
        return SW_ERR_NOT_INIT;
    }
    if (!s_started) {
        (void)pthread_mutex_unlock(&s_mutex);
        return SW_ERR_STATE;
    }
    if (io_manager_is_worker_locked()) {
        io_manager_payload_t local_response;

        (void)pthread_mutex_unlock(&s_mutex);
        memset(&local_response, 0, sizeof(local_response));

        result = s_cfg.execute(s_cfg.execute_ctx, request, request_size, local_response.bytes, response_size);
        if ((result == SW_OK) && (response_size > 0U)) {
            memcpy(response, local_response.bytes, response_size);
        }
        return result;
    }

    slot_index = io_manager_find_slot_locked();
    if (slot_index < 0) {
        (void)pthread_mutex_unlock(&s_mutex);
        return SW_ERR_BUSY;
    }

    slot = &s_slots[slot_index];
    memset(slot, 0, sizeof(*slot));
    slot->used          = true;
    slot->state         = IO_MANAGER_SLOT_QUEUED;
    slot->priority      = priority;
    slot->deadline_ms   = time_util_get_ms() + (uint64_t)timeout_ms;
    slot->request_size  = request_size;
    slot->response_size = response_size;
    if (request_size > 0U) {
        memcpy(slot->request.bytes, request, request_size);
    }
    time_util_fill_deadline(timeout_ms, &wait_deadline);
    (void)pthread_cond_signal(&s_wake);

    while (slot->state != IO_MANAGER_SLOT_DONE) {
        wait_ret = pthread_cond_timedwait(&s_wake, &s_mutex, &wait_deadline);
        if ((wait_ret == ETIMEDOUT) && (slot->state != IO_MANAGER_SLOT_DONE)) {
            slot->waiter_gone = true;
            (void)pthread_mutex_unlock(&s_mutex);
            return SW_ERR_TIMEOUT;
        }
    }

    result = slot->result;
    if ((result == SW_OK) && (response_size > 0U)) {
        memcpy(response, slot->response.bytes, response_size);
    }
    slot->waiter_gone = true;
    io_manager_release_slot_locked(slot);
    (void)pthread_mutex_unlock(&s_mutex);
    return result;
}

bool hal_io_manager_is_worker_thread(void)
{
    bool result;

    io_manager_mutex_ready();
    (void)pthread_mutex_lock(&s_mutex);
    result = io_manager_is_worker_locked();
    (void)pthread_mutex_unlock(&s_mutex);
    return result;
}

#ifdef HAL_IO_MANAGER_UNIT_TEST
#define IO_MANAGER_TEST_STOP_TIMEOUT_MS 1000U

sw_err_t hal_io_manager_reset_for_test(void)
{
    pthread_t       worker;
    struct timespec deadline;
    int             wait_ret = 0;

    io_manager_mutex_ready();
    (void)pthread_mutex_lock(&s_mutex);
    if (s_started) {
        worker           = s_worker;
        s_stop_requested = true;
        (void)pthread_cond_broadcast(&s_wake);
        time_util_fill_deadline(IO_MANAGER_TEST_STOP_TIMEOUT_MS, &deadline);
        while (!s_worker_exited && (wait_ret != ETIMEDOUT)) {
            wait_ret = pthread_cond_timedwait(&s_wake, &s_mutex, &deadline);
        }
        if (!s_worker_exited) {
            (void)pthread_mutex_unlock(&s_mutex);
            return SW_ERR_TIMEOUT;
        }
        (void)pthread_mutex_unlock(&s_mutex);
        (void)pthread_join(worker, NULL);
        (void)pthread_mutex_lock(&s_mutex);
    }
    memset(&s_cfg, 0, sizeof(s_cfg));
    memset(s_slots, 0, sizeof(s_slots));
    memset(&s_worker, 0, sizeof(s_worker));
    s_initialized    = false;
    s_started        = false;
    s_next_tick_ms   = 0U;
    s_stop_requested = false;
    s_worker_exited  = false;
    (void)pthread_mutex_unlock(&s_mutex);
    return SW_OK;
}
#endif
