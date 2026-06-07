/**
 * @file    m8_sensor_setup.c
 * @brief   M8 机型 DI 通道绑定表应用
 * @author  胡望伟
 * @date    2026-06-07
 */

#include "adapters/machine/m8/m8_sensor_setup.h"
#include "config/machine/m8_signal_table.h"
#include "ports/hal/hal_sensor_port.h"
#include "common/log.h"

#ifdef BUILD_SIM
#  include "adapters/hal/sim_hw/hal_sensor_sim.h"
#else
#  include "adapters/hal/linux_hw/hal_sensor_linux.h"
#endif

_Static_assert((unsigned)M8_SIG_MAX <= HAL_SENSOR_CHANNEL_MAX,
               "M8_SIG_MAX 超过 HAL_SENSOR_CHANNEL_MAX，需扩大 hal_sensor 通道上限");

static sw_err_t m8_bind_channel(hal_sensor_channel_t         ch,
                                const hal_sensor_bind_cfg_t *cfg)
{
#ifdef BUILD_SIM
    return hal_sensor_sim_bind(ch, cfg);
#else
    return hal_sensor_linux_bind(ch, cfg);
#endif
}

static sw_err_t m8_apply_signal_table(void)
{
    sw_err_t err = SW_OK;

    for (int i = 0; i < M8_SIGNAL_TABLE_SIZE; ++i)
    {
        const m8_signal_cfg_t *row = &m8_signal_table[i];
        hal_sensor_bind_cfg_t    cfg;
        sw_err_t                 ret;

        cfg.pin            = row->io_id;
        cfg.active_low     = row->active_low;
        cfg.trig_count     = row->trig_count;
        cfg.release_count  = row->release_count;

        /* 通道号 = 行下标 i，合法性由 _Static_assert(M8_SIG_MAX <= HAL_SENSOR_CHANNEL_MAX) 保证 */
        ret = m8_bind_channel((hal_sensor_channel_t)i, &cfg);
        if (ret != SW_OK)
        {
            LOG_ERROR("m8_sensor_setup: bind ch=%d failed ret=%d", i, (int)ret);
            err = ret;
        }
    }

    return err;
}

sw_err_t m8_sensor_setup(void)
{
    const hal_sensor_ops_t *sensor = hal_sensor_get_ops();
    sw_err_t                ret;

    if ((sensor == NULL) || (sensor->init == NULL))
    {
        LOG_ERROR("m8_sensor_setup: hal_sensor ops not registered");
        return SW_ERR_NOT_INIT;
    }

    ret = m8_apply_signal_table();
    if (ret != SW_OK)
    {
        return ret;
    }

    ret = sensor->init();
    if (ret != SW_OK)
    {
        LOG_ERROR("m8_sensor_setup: sensor init failed ret=%d", (int)ret);
        return ret;
    }

    LOG_INFO("m8_sensor_setup: ok, channels=%d", M8_SIGNAL_TABLE_SIZE);
    return SW_OK;
}

sw_err_t m8_sensor_warmup(void)
{
    const hal_sensor_ops_t *sensor = hal_sensor_get_ops();
    uint8_t                 max_trig = 0U;

    if ((sensor == NULL) || (sensor->tick == NULL))
    {
        LOG_ERROR("m8_sensor_warmup: hal_sensor ops not ready");
        return SW_ERR_NOT_INIT;
    }

    for (int i = 0; i < M8_SIGNAL_TABLE_SIZE; ++i)
    {
        if (m8_signal_table[i].trig_count > max_trig)
        {
            max_trig = m8_signal_table[i].trig_count;
        }
    }

    for (uint8_t t = 0U; t < max_trig; t++)
    {
        sensor->tick();
    }

    LOG_INFO("m8_sensor_warmup: ok, ticks=%u", (unsigned)max_trig);
    return SW_OK;
}
