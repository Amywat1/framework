/**
 * @file    hal_sensor_filter.c
 * @brief   DI 通道滤波 HAL 通用适配层实现
 * @author  HUWANGWEI
 * @date    2026-04-10
 *
 * @note    共享状态由 s_sensor_lock 保护，ops.bind()/tick()/is_active()
 *          可在不同线程中并发调用。
 */

#include "framework/adapters/outbound/hal/components/sensor_filter/hal_sensor_filter.h"
#include "framework/ports/outbound/hal/hal_sensor_port.h"
#include "framework/ports/outbound/hal/hal_io_port.h"
#include "framework/common/log.h"

#include <pthread.h>
#include <stddef.h>

#define SENSOR_STABLE_COUNT_MAX  255U

typedef struct
{
    bool    confirmed;
    bool    last_raw;
    uint8_t stable_count;
} sensor_ch_rt_t;

static hal_sensor_bind_cfg_t s_cfg[HAL_SENSOR_CHANNEL_MAX];
static bool                  s_bound[HAL_SENSOR_CHANNEL_MAX];
static sensor_ch_rt_t        s_rt[HAL_SENSOR_CHANNEL_MAX];
static bool                  s_ops_error_logged = false;

static pthread_mutex_t s_sensor_lock = PTHREAD_MUTEX_INITIALIZER;

static bool channel_valid(hal_sensor_channel_t ch)
{
    return (ch < HAL_SENSOR_CHANNEL_MAX);
}

static bool bind_cfg_valid(const hal_sensor_bind_cfg_t *cfg)
{
    if ((cfg == NULL) || (io_di_raw(cfg->pin) == IO_HANDLE_NULL))
    {
        return false;
    }
    if ((cfg->trig_count == 0U) || (cfg->release_count == 0U))
    {
        return false;
    }
    return true;
}

static sw_err_t sensor_bind(hal_sensor_channel_t         ch,
                            const hal_sensor_bind_cfg_t *cfg)
{
    if (!channel_valid(ch) || !bind_cfg_valid(cfg))
    {
        return SW_ERR_PARAM;
    }

    pthread_mutex_lock(&s_sensor_lock);
    s_cfg[ch]   = *cfg;
    s_bound[ch] = true;
    pthread_mutex_unlock(&s_sensor_lock);
    return SW_OK;
}

/* 仅重置运行时滤波状态；通道绑定配置在 bootstrap 阶段由 ops.bind() 写入，不在此清除 */
static sw_err_t sensor_init(void)
{
    pthread_mutex_lock(&s_sensor_lock);
    for (hal_sensor_channel_t ch = 0U; ch < HAL_SENSOR_CHANNEL_MAX; ch++)
    {
        s_rt[ch].confirmed    = false;
        s_rt[ch].last_raw     = false;
        s_rt[ch].stable_count = 0U;
    }

    s_ops_error_logged = false;
    pthread_mutex_unlock(&s_sensor_lock);
    return SW_OK;
}

static void sensor_tick(void)
{
    const hal_io_ops_t *io = hal_io_get_ops();

    if ((io == NULL) || (io->di_read == NULL))
    {
        pthread_mutex_lock(&s_sensor_lock);
        if (!s_ops_error_logged)
        {
            LOG_ERROR("hal_sensor: hal_io ops not ready");
            s_ops_error_logged = true;
        }
        pthread_mutex_unlock(&s_sensor_lock);
        return;
    }

    pthread_mutex_lock(&s_sensor_lock);

    s_ops_error_logged = false;

    for (hal_sensor_channel_t ch = 0U; ch < HAL_SENSOR_CHANNEL_MAX; ch++)
    {
        const hal_sensor_bind_cfg_t *cfg = &s_cfg[ch];
        sensor_ch_rt_t              *rt  = &s_rt[ch];
        bool                         di_val;
        bool                         raw_active;
        uint8_t                      threshold;

        if (!s_bound[ch])
        {
            continue;
        }

        di_val     = io->di_read(cfg->pin);
        raw_active = cfg->active_low ? (!di_val) : di_val;

        if (raw_active == rt->last_raw)
        {
            if (rt->stable_count < SENSOR_STABLE_COUNT_MAX)
            {
                rt->stable_count++;
            }
        }
        else
        {
            rt->last_raw     = raw_active;
            rt->stable_count = 1U;
        }

        threshold = raw_active ? cfg->trig_count : cfg->release_count;
        if ((rt->stable_count >= threshold) && (rt->confirmed != raw_active))
        {
            rt->confirmed = raw_active;
        }
    }

    pthread_mutex_unlock(&s_sensor_lock);
}

static bool sensor_is_active(hal_sensor_channel_t ch)
{
    bool active;

    if (!channel_valid(ch))
    {
        return false;
    }

    pthread_mutex_lock(&s_sensor_lock);
    active = s_bound[ch] ? s_rt[ch].confirmed : false;
    pthread_mutex_unlock(&s_sensor_lock);

    return active;
}

static const hal_sensor_ops_t s_ops = {
    .init      = sensor_init,
    .bind      = sensor_bind,
    .tick      = sensor_tick,
    .is_active = sensor_is_active,
};

void hal_sensor_filter_register(void)
{
    hal_sensor_register(&s_ops);
}
