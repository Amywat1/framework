/**
 * @file    lifetime.c
 * @brief   保养通道表：累秒或边沿计数，并落盘
 */

#include "application/lifetime.h"

#include "common/log.h"
#include "common/sw_mutex.h"
#include "common/time_util.h"
#include "domain/ports/outbound/storage/param_kv.h"
#include "runtime/config/thread_config.h"
#include "runtime/scheduler/periodic_task.h"

#include <limits.h>
#include <pthread.h>
#include <sched.h>
#include <string.h>

static const uint64_t k_run_ms_cap = (uint64_t)INT32_MAX * 1000ULL;

typedef struct {
    uint64_t acc_ms;
    int32_t  count;
    bool     was_active;
    bool     dirty;
} lifetime_acc_t;

static pthread_mutex_t           s_mu;
static pthread_once_t            s_mu_once = PTHREAD_ONCE_INIT;
static const lifetime_channel_t *s_channels;
static unsigned                  s_count;
static bool                      s_initialized;
static bool                      s_have_sample;
static uint64_t                  s_last_ms;
static uint64_t                  s_last_save_ms;
static lifetime_acc_t            s_acc[LIFETIME_CHANNEL_MAX];

static void lifetime_mu_init(void)
{
    (void)sw_mutex_init_prio_inherit(&s_mu);
}

static void lifetime_lock(void)
{
    (void)pthread_once(&s_mu_once, lifetime_mu_init);
    pthread_mutex_lock(&s_mu);
}

static void lifetime_unlock(void)
{
    pthread_mutex_unlock(&s_mu);
}

static int32_t seconds_from_ms(uint64_t ms)
{
    uint64_t sec = ms / 1000ULL;

    if (sec > (uint64_t)INT32_MAX) {
        return INT32_MAX;
    }
    return (int32_t)sec;
}

static uint64_t ms_from_seconds(int raw)
{
    if (raw <= 0) {
        return 0U;
    }
    if (raw >= INT32_MAX) {
        return k_run_ms_cap;
    }
    return (uint64_t)raw * 1000ULL;
}

static void add_run_ms(uint64_t *acc, uint64_t dt)
{
    if (*acc >= k_run_ms_cap) {
        *acc = k_run_ms_cap;
        return;
    }
    if (dt > (k_run_ms_cap - *acc)) {
        *acc = k_run_ms_cap;
        return;
    }
    *acc += dt;
}

static int32_t load_count(const char *key)
{
    int raw = param_kv_get_int(key, 0);

    if (raw < 0) {
        return 0;
    }
    if (raw > INT32_MAX) {
        return INT32_MAX;
    }
    return raw;
}

static int32_t value_of_locked(unsigned i)
{
    if (s_channels[i].on_edge) {
        return s_acc[i].count;
    }
    return seconds_from_ms(s_acc[i].acc_ms);
}

static void load_locked(void)
{
    unsigned i;

    memset(s_acc, 0, sizeof(s_acc));
    for (i = 0U; i < s_count; i++) {
        if (s_channels[i].on_edge) {
            s_acc[i].count = load_count(s_channels[i].kv_key);
        } else {
            s_acc[i].acc_ms = ms_from_seconds(param_kv_get_int(s_channels[i].kv_key, 0));
        }
    }
}

static void persist_locked(void)
{
    unsigned i;

    for (i = 0U; i < s_count; i++) {
        (void)param_kv_set_int(s_channels[i].kv_key, value_of_locked(i));
        s_acc[i].dirty = false;
    }
    if (param_kv_save() != SW_OK) {
        LOG_WARN("lifetime: persist failed");
    }
}

static sw_err_t validate_channels(const lifetime_channel_t *channels, unsigned count)
{
    unsigned i;
    unsigned j;

    if ((channels == NULL) || (count == 0U)) {
        return SW_ERR_PARAM;
    }
    if (count > LIFETIME_CHANNEL_MAX) {
        return SW_ERR_OVERFLOW;
    }
    for (i = 0U; i < count; i++) {
        if ((channels[i].kv_key == NULL) || (channels[i].kv_key[0] == '\0')
            || (channels[i].is_active == NULL)) {
            return SW_ERR_PARAM;
        }
        for (j = 0U; j < i; j++) {
            if (strcmp(channels[i].kv_key, channels[j].kv_key) == 0) {
                return SW_ERR_PARAM;
            }
        }
    }
    return SW_OK;
}

sw_err_t lifetime_bind(const lifetime_channel_t *channels, unsigned count)
{
    sw_err_t ret = validate_channels(channels, count);

    if (ret != SW_OK) {
        return ret;
    }

    lifetime_lock();
    if ((s_channels == channels) && (s_count == count)) {
        lifetime_unlock();
        return SW_OK;
    }
    s_channels = channels;
    s_count    = count;
    if (s_initialized) {
        load_locked();
        s_have_sample  = false;
        s_last_ms      = 0U;
        s_last_save_ms = 0U;
    }
    lifetime_unlock();
    return SW_OK;
}

sw_err_t lifetime_init(void)
{
    lifetime_lock();
    if (!s_initialized) {
        load_locked();
        s_have_sample  = false;
        s_last_ms      = 0U;
        s_last_save_ms = 0U;
        s_initialized  = true;
    }
    lifetime_unlock();
    return SW_OK;
}

void lifetime_poll(uint64_t now_ms)
{
    unsigned i;
    bool     persist_now = false;
    bool     seconds_dirty = false;

    (void)lifetime_init();

    lifetime_lock();
    if (s_have_sample && (now_ms > s_last_ms)) {
        uint64_t dt = now_ms - s_last_ms;

        for (i = 0U; i < s_count; i++) {
            bool active = s_channels[i].is_active();

            if (s_channels[i].on_edge) {
                if (active && !s_acc[i].was_active) {
                    if (s_acc[i].count < INT32_MAX) {
                        s_acc[i].count++;
                    }
                    persist_now = true;
                }
            } else if (active) {
                add_run_ms(&s_acc[i].acc_ms, dt);
                s_acc[i].dirty = true;
                seconds_dirty  = true;
            }
            s_acc[i].was_active = active;
        }
        if (seconds_dirty
            && ((s_last_save_ms == 0U)
                || ((now_ms - s_last_save_ms) >= LIFETIME_SAVE_PERIOD_MS))) {
            persist_now = true;
        }
    } else {
        for (i = 0U; i < s_count; i++) {
            s_acc[i].was_active = s_channels[i].is_active();
        }
    }

    s_last_ms     = now_ms;
    s_have_sample = true;
    if (persist_now) {
        persist_locked();
        s_last_save_ms = now_ms;
    }
    lifetime_unlock();
}

sw_err_t lifetime_get_int(const char *kv_key, int32_t *out)
{
    unsigned i;

    if ((kv_key == NULL) || (out == NULL)) {
        return SW_ERR_PARAM;
    }

    lifetime_lock();
    if (!s_initialized) {
        lifetime_unlock();
        return SW_ERR_NOT_INIT;
    }
    for (i = 0U; i < s_count; i++) {
        if (strcmp(s_channels[i].kv_key, kv_key) == 0) {
            *out = value_of_locked(i);
            lifetime_unlock();
            return SW_OK;
        }
    }
    lifetime_unlock();
    return SW_ERR_NOT_FOUND;
}

static void lifetime_tick_fn(void *ctx)
{
    (void)ctx;
    lifetime_poll(time_util_get_ms());
}

sw_err_t lifetime_register_task(void)
{
    sw_err_t ret = lifetime_init();

    if (ret != SW_OK) {
        return ret;
    }
    return periodic_task_register("lifetime",
                                  LIFETIME_POLL_MS,
                                  lifetime_tick_fn,
                                  NULL,
                                  SCHED_OTHER,
                                  0,
                                  THD_SENSOR_POLL_STACK);
}

void lifetime_reset_for_test(void)
{
    lifetime_lock();
    s_channels     = NULL;
    s_count        = 0U;
    s_initialized  = false;
    s_have_sample  = false;
    s_last_ms      = 0U;
    s_last_save_ms = 0U;
    memset(s_acc, 0, sizeof(s_acc));
    lifetime_unlock();
}
