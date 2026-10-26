/**
 * @file    comm_watchdog.c
 * @brief   周期通讯心跳轮询器
 */

#include "application/comm_watchdog.h"

#include "application/ports/inbound/safety/alarm_binding_port.h"
#include "common/sw_mutex.h"
#include "common/time_util.h"

#include <pthread.h>
#include <stdbool.h>
#include <string.h>

static pthread_mutex_t                 s_mu;
static pthread_once_t                  s_mu_once = PTHREAD_ONCE_INIT;
static const comm_watchdog_channel_t  *s_channels;
static unsigned                        s_count;
static comm_watchdog_on_lost_fn_t      s_on_lost;
static bool                            s_initialized;
static uint64_t                        s_last_hb_ms[COMM_WATCHDOG_CHANNEL_MAX];
static bool                            s_lost_active[COMM_WATCHDOG_CHANNEL_MAX];

static void comm_wd_mu_init(void)
{
    (void)sw_mutex_init_prio_inherit(&s_mu);
}

static void comm_wd_lock(void)
{
    (void)pthread_once(&s_mu_once, comm_wd_mu_init);
    pthread_mutex_lock(&s_mu);
}

static void comm_wd_unlock(void)
{
    pthread_mutex_unlock(&s_mu);
}

static void stamp_now_locked(void)
{
    uint64_t now = time_util_get_ms();
    unsigned i;

    for (i = 0U; i < s_count; i++) {
        s_last_hb_ms[i]  = now;
        s_lost_active[i] = false;
    }
}

static sw_err_t validate_channels(const comm_watchdog_channel_t *channels, unsigned count)
{
    unsigned i;

    if ((channels == NULL) || (count == 0U)) {
        return SW_ERR_PARAM;
    }
    if (count > COMM_WATCHDOG_CHANNEL_MAX) {
        return SW_ERR_OVERFLOW;
    }
    for (i = 0U; i < count; i++) {
        if (channels[i].timeout_ms == 0U) {
            return SW_ERR_PARAM;
        }
    }
    return SW_OK;
}

sw_err_t comm_watchdog_bind(const comm_watchdog_channel_t *channels, unsigned count,
                            comm_watchdog_on_lost_fn_t on_lost)
{
    sw_err_t ret = validate_channels(channels, count);

    if (ret != SW_OK) {
        return ret;
    }

    comm_wd_lock();
    if ((s_channels == channels) && (s_count == count)) {
        s_on_lost = on_lost;
        comm_wd_unlock();
        return SW_OK;
    }
    s_channels = channels;
    s_count    = count;
    s_on_lost  = on_lost;
    if (s_initialized) {
        stamp_now_locked();
    }
    comm_wd_unlock();
    return SW_OK;
}

void comm_watchdog_init(void)
{
    comm_wd_lock();
    stamp_now_locked();
    s_initialized = true;
    comm_wd_unlock();
}

void comm_watchdog_heartbeat(unsigned id)
{
    comm_wd_lock();
    if (id < s_count) {
        s_last_hb_ms[id] = time_util_get_ms();
    }
    comm_wd_unlock();
}

void comm_watchdog_poll(void)
{
    const alarm_binding_ops_t *ops;
    uint64_t                   now;
    unsigned                   i;
    unsigned                   n_lost  = 0U;
    unsigned                   n_trig  = 0U;
    unsigned                   n_clear = 0U;
    unsigned                   lost_ids[COMM_WATCHDOG_CHANNEL_MAX];
    uint32_t                   trig_codes[COMM_WATCHDOG_CHANNEL_MAX];
    uint32_t                   clear_codes[COMM_WATCHDOG_CHANNEL_MAX];
    unsigned                   clear_ids[COMM_WATCHDOG_CHANNEL_MAX];
    comm_watchdog_on_lost_fn_t on_lost;

    ops = alarm_binding_get_ops();
    if ((ops == NULL) || (ops->trigger == NULL)) {
        return;
    }

    now = time_util_get_ms();
    comm_wd_lock();
    on_lost = s_on_lost;
    for (i = 0U; i < s_count; i++) {
        bool lost = (time_elapsed_ms(s_last_hb_ms[i], now) > s_channels[i].timeout_ms);

        if (lost) {
            if (!s_lost_active[i]) {
                s_lost_active[i]     = true;
                lost_ids[n_lost++]   = i;
            }
            trig_codes[n_trig++] = s_channels[i].alarm_code;
        } else if (s_lost_active[i] && (ops->clear != NULL)) {
            clear_ids[n_clear]     = i;
            clear_codes[n_clear]   = s_channels[i].alarm_code;
            n_clear++;
        }
    }
    comm_wd_unlock();

    for (i = 0U; i < n_lost; i++) {
        if (on_lost != NULL) {
            on_lost(lost_ids[i]);
        }
    }
    for (i = 0U; i < n_trig; i++) {
        (void)ops->trigger(trig_codes[i]);
    }
    for (i = 0U; i < n_clear; i++) {
        if (ops->clear(clear_codes[i]) == SW_OK) {
            comm_wd_lock();
            if ((clear_ids[i] < s_count) && s_lost_active[clear_ids[i]]) {
                s_lost_active[clear_ids[i]] = false;
            }
            comm_wd_unlock();
        }
    }
}

void comm_watchdog_reset_for_test(void)
{
    comm_wd_lock();
    s_channels    = NULL;
    s_count       = 0U;
    s_on_lost     = NULL;
    s_initialized = false;
    memset(s_last_hb_ms, 0, sizeof(s_last_hb_ms));
    memset(s_lost_active, 0, sizeof(s_lost_active));
    comm_wd_unlock();
}
