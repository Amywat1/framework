/**
 * @file    pending_start.c
 * @brief   未准入先点启动的门锁与就位延迟放行
 */

#include "application/pending_start.h"

#include "application/job_admit.h"
#include "common/log.h"
#include "common/sw_mutex.h"
#include "common/time_util.h"
#include "domain/job/order_book.h"
#include "domain/ports/outbound/device/device_ops_port.h"
#include "domain/ports/outbound/device/entry_occupancy_port.h"
#include "domain/ports/outbound/storage/param_kv.h"
#include "domain/telemetry/device_snapshot.h"
#include "runtime/config/thread_config.h"
#include "runtime/scheduler/periodic_task.h"

#include <pthread.h>
#include <sched.h>

static pthread_mutex_t s_mu;
static pthread_once_t  s_mu_once = PTHREAD_ONCE_INIT;
static bool            s_armed;
static bool            s_seen_vehicle;
static uint64_t        s_ready_since_ms;

static void pending_mu_init(void)
{
    (void)sw_mutex_init_prio_inherit(&s_mu);
}

static void pending_lock(void)
{
    (void)pthread_once(&s_mu_once, pending_mu_init);
    pthread_mutex_lock(&s_mu);
}

static void pending_unlock(void)
{
    pthread_mutex_unlock(&s_mu);
}

static void clear_locked(void)
{
    s_armed          = false;
    s_seen_vehicle   = false;
    s_ready_since_ms = 0U;
}

/**
 * @brief  下一单是否可放行；未注册视为已准入
 */
static bool admit_ready(void)
{
    const device_ops_t *ops = device_ops_get();

    if ((ops == NULL) || (ops->is_admit_ready == NULL)) {
        return true;
    }
    return ops->is_admit_ready();
}

uint32_t pending_start_delay_ms(void)
{
    int raw = param_kv_get_int(PARAM_KEY_PENDING_START_DELAY_MS, 0);

    if (raw <= 0) {
        return 0U;
    }
    if (raw > (int)PENDING_START_DELAY_MAX_MS) {
        return PENDING_START_DELAY_MAX_MS;
    }
    return (uint32_t)raw;
}

void pending_start_init(void)
{
    pending_lock();
    clear_locked();
    pending_unlock();
}

void pending_start_on_start_result(sw_err_t start_ret)
{
    uint32_t           delay_ms;
    op_reject_reason_t reason;

    if (start_ret == SW_OK) {
        pending_lock();
        clear_locked();
        pending_unlock();
        return;
    }

    delay_ms = pending_start_delay_ms();
    reason   = job_admit_last_fail_reason();
    if ((start_ret != SW_ERR_STATE) || (delay_ms == 0U) || (reason != OP_REJECT_ENTRY_NOT_READY)) {
        return;
    }

    pending_lock();
    s_armed          = true;
    s_seen_vehicle   = (entry_occupancy_get().phase != ENTRY_OCC_EMPTY);
    s_ready_since_ms = 0U;
    pending_unlock();
    LOG_INFO("pending_start: 记住未准入启动，准入后再等 %u ms", delay_ms);
}

void pending_start_tick(void)
{
    operational_mode_t mode;
    entry_occupancy_t  occ;
    uint32_t           delay_ms;
    uint64_t           now_ms;
    bool               should_start = false;

    delay_ms = pending_start_delay_ms();
    mode     = device_snapshot_get().op.mode;
    occ      = entry_occupancy_get();
    now_ms   = time_util_get_ms();

    pending_lock();
    if (!s_armed) {
        pending_unlock();
        return;
    }

    if ((delay_ms == 0U) || (mode != OP_MODE_STANDBY) || (order_book_count() == 0U)) {
        clear_locked();
        pending_unlock();
        return;
    }

    if (occ.available && (occ.phase == ENTRY_OCC_EMPTY)) {
        if (s_seen_vehicle) {
            clear_locked();
        }
        s_ready_since_ms = 0U;
        pending_unlock();
        return;
    }

    if (occ.available && (occ.phase != ENTRY_OCC_EMPTY)) {
        s_seen_vehicle = true;
    }

    if (!admit_ready()) {
        s_ready_since_ms = 0U;
        pending_unlock();
        return;
    }

    if (s_ready_since_ms == 0U) {
        s_ready_since_ms = (now_ms == 0U) ? 1U : now_ms;
        pending_unlock();
        return;
    }

    if (time_elapsed_ms(s_ready_since_ms, now_ms) >= delay_ms) {
        should_start = true;
    }
    pending_unlock();

    if (!should_start) {
        return;
    }

    if (job_admit_start() == SW_OK) {
        pending_lock();
        clear_locked();
        pending_unlock();
        LOG_INFO("pending_start: 准入延迟到，已放行队首");
    }
}

bool pending_start_is_armed(void)
{
    bool armed;

    pending_lock();
    armed = s_armed;
    pending_unlock();
    return armed;
}

static void pending_tick_fn(void *ctx)
{
    (void)ctx;
    pending_start_tick();
}

sw_err_t pending_start_register_task(void)
{
    pending_start_init();
    return periodic_task_register("pending_start",
                                  PENDING_START_TICK_MS,
                                  pending_tick_fn,
                                  NULL,
                                  SCHED_OTHER,
                                  0,
                                  THD_SENSOR_POLL_STACK);
}
