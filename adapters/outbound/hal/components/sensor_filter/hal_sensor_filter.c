/**
 * @file    hal_sensor_filter.c
 * @brief   DI 通道滤波 HAL 通用适配层实现
 * @author  HUWANGWEI
 * @date    2026-04-10
 *
 * @note    共享状态由 s_sensor_lock 保护，hal_sensor_filter_bind()/ops.warmup()/is_active()
 *          可在不同线程中并发调用。
 */

#include "adapters/outbound/hal/components/sensor_filter/hal_sensor_filter.h"

#include "common/log.h"
#include "ports/outbound/hal/hal_io_port.h"
#include "ports/outbound/hal/hal_sensor_port.h"
#include "runtime/config/thread_config.h"
#include "runtime/scheduler/periodic_task.h"

#include <pthread.h>
#include <sched.h>
#include <stddef.h>

#define SENSOR_STABLE_COUNT_MAX   255U
#define HAL_SENSOR_POLL_PERIOD_MS 30U
#define HAL_SENSOR_OBSERVER_MAX   4U

typedef struct {
    hal_sensor_state_t state;
    bool               last_raw;
    uint8_t            stable_count;
} sensor_ch_rt_t;

typedef struct {
    hal_sensor_state_cb_t cb;
    void                 *ctx;
} sensor_observer_t;

typedef struct {
    hal_sensor_channel_t ch;
    hal_sensor_state_t   state;
} sensor_transition_t;

static hal_sensor_bind_cfg_t s_cfg[HAL_SENSOR_CHANNEL_MAX];
static bool                  s_bound[HAL_SENSOR_CHANNEL_MAX];
static sensor_ch_rt_t        s_rt[HAL_SENSOR_CHANNEL_MAX];
static bool                  s_ops_error_logged = false;
static bool                  s_initialized      = false;
static sensor_observer_t     s_observers[HAL_SENSOR_OBSERVER_MAX];
static size_t                s_observer_count = 0U;

static pthread_mutex_t s_sensor_lock = PTHREAD_MUTEX_INITIALIZER;

static bool channel_valid(hal_sensor_channel_t ch)
{
    return (ch < HAL_SENSOR_CHANNEL_MAX);
}

static bool bind_cfg_valid(const hal_sensor_bind_cfg_t *cfg)
{
    if ((cfg == NULL) || (io_di_raw(cfg->pin) == IO_HANDLE_NULL)) {
        return false;
    }
    if ((cfg->trig_count == 0U) || (cfg->release_count == 0U)) {
        return false;
    }
    return true;
}

sw_err_t hal_sensor_filter_bind(hal_sensor_channel_t ch, const hal_sensor_bind_cfg_t *cfg)
{
    if (!channel_valid(ch) || !bind_cfg_valid(cfg)) {
        LOG_ERROR("hal_sensor: bind param invalid ch=%u", (unsigned)ch);
        return SW_ERR_PARAM;
    }

    pthread_mutex_lock(&s_sensor_lock);
    if (s_bound[ch]) {
        pthread_mutex_unlock(&s_sensor_lock);
        return SW_ERR_BUSY;
    }
    s_cfg[ch]     = *cfg;
    s_bound[ch]   = true;
    s_initialized = false;
    pthread_mutex_unlock(&s_sensor_lock);
    return SW_OK;
}

/* 仅重置运行时滤波状态；通道绑定配置在 bootstrap 阶段写入，不在此清除 */
static sw_err_t sensor_init(void)
{
    bool any_bound = false;

    pthread_mutex_lock(&s_sensor_lock);
    for (hal_sensor_channel_t ch = 0U; ch < HAL_SENSOR_CHANNEL_MAX; ch++) {
        if (s_bound[ch]) {
            any_bound = true;
        }
        s_rt[ch].state        = HAL_SENSOR_STATE_UNKNOWN;
        s_rt[ch].last_raw     = false;
        s_rt[ch].stable_count = 0U;
    }

    if (!any_bound) {
        pthread_mutex_unlock(&s_sensor_lock);
        return SW_ERR_NOT_INIT;
    }

    s_ops_error_logged = false;
    s_initialized      = true;
    pthread_mutex_unlock(&s_sensor_lock);
    return SW_OK;
}

static sw_err_t sensor_tick(void)
{
    const hal_io_ops_t *io = hal_io_get_ops();
    sensor_transition_t transitions[HAL_SENSOR_CHANNEL_MAX];
    sensor_observer_t   observers[HAL_SENSOR_OBSERVER_MAX];
    size_t              transition_count = 0U;
    size_t              observer_count;

    pthread_mutex_lock(&s_sensor_lock);
    if (!s_initialized) {
        pthread_mutex_unlock(&s_sensor_lock);
        return SW_ERR_NOT_INIT;
    }
    pthread_mutex_unlock(&s_sensor_lock);

    if ((io == NULL) || (io->di_read == NULL)) {
        pthread_mutex_lock(&s_sensor_lock);
        if (!s_ops_error_logged) {
            LOG_ERROR("hal_sensor: hal_io ops not ready");
            s_ops_error_logged = true;
        }
        pthread_mutex_unlock(&s_sensor_lock);
        return SW_ERR_NOT_INIT;
    }

    pthread_mutex_lock(&s_sensor_lock);

    s_ops_error_logged = false;

    for (hal_sensor_channel_t ch = 0U; ch < HAL_SENSOR_CHANNEL_MAX; ch++) {
        const hal_sensor_bind_cfg_t *cfg = &s_cfg[ch];
        sensor_ch_rt_t              *rt  = &s_rt[ch];
        io_di_sample_t               sample;
        bool                         raw_active;
        uint8_t                      threshold;
        hal_sensor_state_t           next_state;

        if (!s_bound[ch]) {
            continue;
        }

        if ((io->di_read(cfg->pin, &sample) != SW_OK) || (sample.quality != IO_SAMPLE_QUALITY_VALID)) {
            rt->last_raw     = false;
            rt->stable_count = 0U;
            if (rt->state != HAL_SENSOR_STATE_UNKNOWN) {
                rt->state                       = HAL_SENSOR_STATE_UNKNOWN;
                transitions[transition_count++] = (sensor_transition_t){ch, HAL_SENSOR_STATE_UNKNOWN};
            }
            continue;
        }

        raw_active = cfg->active_low ? (!sample.level) : sample.level;

        if (raw_active == rt->last_raw) {
            if (rt->stable_count < SENSOR_STABLE_COUNT_MAX) {
                rt->stable_count++;
            }
        } else {
            rt->last_raw     = raw_active;
            rt->stable_count = 1U;
        }

        threshold  = raw_active ? cfg->trig_count : cfg->release_count;
        next_state = raw_active ? HAL_SENSOR_STATE_ACTIVE : HAL_SENSOR_STATE_INACTIVE;
        if ((rt->stable_count >= threshold) && (rt->state != next_state)) {
            rt->state                       = next_state;
            transitions[transition_count++] = (sensor_transition_t){ch, next_state};
        }
    }

    observer_count = s_observer_count;
    for (size_t i = 0U; i < observer_count; ++i) {
        observers[i] = s_observers[i];
    }
    pthread_mutex_unlock(&s_sensor_lock);

    for (size_t i = 0U; i < transition_count; ++i) {
        for (size_t j = 0U; j < observer_count; ++j) {
            observers[j].cb(transitions[i].ch, transitions[i].state, observers[j].ctx);
        }
    }
    return SW_OK;
}

static sw_err_t sensor_warmup(uint8_t sample_count)
{
    sw_err_t ret = SW_OK;

    for (uint8_t i = 0U; i < sample_count; i++) {
        ret = sensor_tick();
        if (ret != SW_OK) {
            return ret;
        }
    }

    return ret;
}

static bool sensor_is_active(hal_sensor_channel_t ch)
{
    bool active;

    if (!channel_valid(ch)) {
        return false;
    }

    pthread_mutex_lock(&s_sensor_lock);
    active = s_bound[ch] && (s_rt[ch].state == HAL_SENSOR_STATE_ACTIVE);
    pthread_mutex_unlock(&s_sensor_lock);

    return active;
}

static hal_sensor_state_t sensor_get_state(hal_sensor_channel_t ch)
{
    hal_sensor_state_t state = HAL_SENSOR_STATE_UNKNOWN;

    if (!channel_valid(ch)) {
        return state;
    }

    pthread_mutex_lock(&s_sensor_lock);
    if (s_bound[ch]) {
        state = s_rt[ch].state;
    }
    pthread_mutex_unlock(&s_sensor_lock);
    return state;
}

static sw_err_t sensor_subscribe(hal_sensor_state_cb_t cb, void *ctx)
{
    if (cb == NULL) {
        return SW_ERR_PARAM;
    }

    pthread_mutex_lock(&s_sensor_lock);
    if (s_observer_count >= HAL_SENSOR_OBSERVER_MAX) {
        pthread_mutex_unlock(&s_sensor_lock);
        return SW_ERR_OVERFLOW;
    }
    s_observers[s_observer_count++] = (sensor_observer_t){cb, ctx};
    pthread_mutex_unlock(&s_sensor_lock);
    return SW_OK;
}

static const hal_sensor_ops_t s_ops = {
    .init      = sensor_init,
    .warmup    = sensor_warmup,
    .is_active = sensor_is_active,
    .get_state = sensor_get_state,
    .subscribe = sensor_subscribe,
};

void hal_sensor_filter_register(void)
{
    hal_sensor_register(&s_ops);
}

#ifdef HAL_SENSOR_FILTER_UNIT_TEST
void hal_sensor_filter_test_reset(void)
{
    pthread_mutex_lock(&s_sensor_lock);
    for (hal_sensor_channel_t ch = 0U; ch < HAL_SENSOR_CHANNEL_MAX; ch++) {
        s_cfg[ch]             = (hal_sensor_bind_cfg_t){0};
        s_bound[ch]           = false;
        s_rt[ch].state        = HAL_SENSOR_STATE_UNKNOWN;
        s_rt[ch].last_raw     = false;
        s_rt[ch].stable_count = 0U;
    }
    s_ops_error_logged = false;
    s_initialized      = false;
    s_observer_count   = 0U;
    pthread_mutex_unlock(&s_sensor_lock);
}

/**
 * @brief  直接驱动一次滤波 tick（仅测试）
 *
 * @note   与 hal_vfd_manager_test_tick 同一用途：tick 回调是 static，且经
 *         scheduler 启动后跑在独立线程里，测试无法确定地驱动它。导出本入口
 *         使 tick 路径可在单线程下被断言（例如 ARCH-15 的零分配验证）。
 */
sw_err_t hal_sensor_filter_test_tick(void)
{
    return sensor_tick();
}
#endif

static void sensor_poll_task(void *ctx)
{
    (void)ctx;
    (void)sensor_tick();
}

sw_err_t hal_sensor_poll_register_task(void)
{
    return periodic_task_register(
        "hal_sensor_poll", HAL_SENSOR_POLL_PERIOD_MS, sensor_poll_task, NULL, SCHED_OTHER, 0, THD_SENSOR_POLL_STACK);
}
