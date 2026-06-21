/**
 * @file    hal_sensor.c
 * @brief   DI 通道滤波 HAL 端口实现（依赖 hal_io_port，无平台 SDK）
 * @author  HUWANGWEI
 * @date    2026-04-10
 */

#include "adapters/hal/generic/hal_sensor.h"
#include "ports/hal/hal_sensor_port.h"
#include "ports/hal/hal_io_port.h"
#include "common/log.h"

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

sw_err_t hal_sensor_bind(hal_sensor_channel_t         ch,
                         const hal_sensor_bind_cfg_t *cfg)
{
    if (!channel_valid(ch) || !bind_cfg_valid(cfg))
    {
        return SW_ERR_PARAM;
    }

    s_cfg[ch]   = *cfg;
    s_bound[ch] = true;
    return SW_OK;
}

static sw_err_t sensor_init(void)
{
    for (hal_sensor_channel_t ch = 0U; ch < HAL_SENSOR_CHANNEL_MAX; ch++)
    {
        s_rt[ch].confirmed    = false;
        s_rt[ch].last_raw     = false;
        s_rt[ch].stable_count = 0U;
    }

    s_ops_error_logged = false;
    return SW_OK;
}

static void sensor_tick(void)
{
    const hal_io_ops_t *io = hal_io_get_ops();

    if ((io == NULL) || (io->di_read == NULL))
    {
        if (!s_ops_error_logged)
        {
            LOG_ERROR("hal_sensor: hal_io ops not ready");
            s_ops_error_logged = true;
        }
        return;
    }

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
}

static bool sensor_is_active(hal_sensor_channel_t ch)
{
    if (!channel_valid(ch) || !s_bound[ch])
    {
        return false;
    }

    return s_rt[ch].confirmed;
}

static const hal_sensor_ops_t s_ops = {
    .init      = sensor_init,
    .tick      = sensor_tick,
    .is_active = sensor_is_active,
};

void hal_sensor_generic_register(void)
{
    hal_sensor_register(&s_ops);
}
