/**
 * @file    m8_sensor.c
 * @brief   M8 传感器子系统实现（DI 绑定、预热、信号查询）
 * @author  HUWANGWEI
 * @date    2026-06-21
 */

#include "projects/m8/bindings/m8_sensor.h"
#include "projects/m8/config/m8_signal_table.h"
#include "framework/ports/outbound/hal/hal_sensor_port.h"
#include "framework/common/log.h"

_Static_assert((unsigned)M8_SIG_MAX <= HAL_SENSOR_CHANNEL_MAX,
               "M8_SIG_MAX 超过 HAL_SENSOR_CHANNEL_MAX，需扩大 hal_sensor 通道上限");

/* -------------------------------------------------------------------------
 * 初始化
 * ------------------------------------------------------------------------- */

static sw_err_t apply_signal_table(const hal_sensor_ops_t *sensor)
{
    sw_err_t err = SW_OK;

    for (int i = 0; i < M8_SIGNAL_TABLE_SIZE; ++i)
    {
        const m8_signal_cfg_t *row = &m8_signal_table[i];
        hal_sensor_bind_cfg_t  cfg;
        sw_err_t               ret;

        cfg.pin           = row->io_id;
        cfg.active_low    = row->active_low;
        cfg.trig_count    = row->trig_count;
        cfg.release_count = row->release_count;

        ret = sensor->bind((hal_sensor_channel_t)i, &cfg);
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

    if ((sensor == NULL) || (sensor->init == NULL) || (sensor->bind == NULL))
    {
        LOG_ERROR("m8_sensor_setup: hal_sensor ops not registered");
        return SW_ERR_NOT_INIT;
    }

    ret = apply_signal_table(sensor);
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

/* -------------------------------------------------------------------------
 * 运行时查询
 * ------------------------------------------------------------------------- */

bool m8_signal_is_active(m8_signal_id_t sig_id)
{
    const hal_sensor_ops_t *sensor = hal_sensor_get_ops();

    if (((int)sig_id < 0) || ((int)sig_id >= M8_SIGNAL_TABLE_SIZE))
    {
        return false;
    }

    if ((sensor == NULL) || (sensor->is_active == NULL))
    {
        return false;
    }

    return sensor->is_active((hal_sensor_channel_t)sig_id);
}
