/**
 * @file    hal_vfd_linux.c
 * @brief   变频器 HAL 端口 Linux 真机实现（通用 drv_vfd 转发）
 * @author  HUWANGWEI
 * @date    2026-06-01
 */

#include "adapters/hal/linux_hw/hal_vfd_linux.h"
#include "ports/hal/hal_vfd_port.h"
#include "adapters/hal/linux_hw/drv/drv_io.h"
#include "adapters/hal/linux_hw/drv/drv_vfd.h"

#include <string.h>

#define VFD_LINUX_SLOT_COUNT    8U

static drv_vfd_t  s_vfd[VFD_LINUX_SLOT_COUNT];
static bool       s_vfd_bound[VFD_LINUX_SLOT_COUNT];

static sw_err_t vfd_do_set(io_do_t pin, bool val)
{
    return drv_io_do_set(pin, val);
}

static bool vfd_id_valid(hal_vfd_id_t id)
{
    return ((unsigned)id < VFD_LINUX_SLOT_COUNT);
}

static drv_vfd_t *vfd_by_id(hal_vfd_id_t id)
{
    if (!vfd_id_valid(id) || !s_vfd_bound[(unsigned)id])
    {
        return NULL;
    }
    return &s_vfd[(unsigned)id];
}

sw_err_t hal_vfd_linux_instance_init(hal_vfd_id_t  id,
                                     const char   *serial_port,
                                     int           baud,
                                     int           modbus_addr,
                                     io_do_t       pin_fwd,
                                     io_do_t       pin_rev,
                                     io_do_t       pin_rst)
{
    sw_err_t ret;

    if (!vfd_id_valid(id))
    {
        return SW_ERR_PARAM;
    }

    memset(&s_vfd[(unsigned)id], 0, sizeof(s_vfd[(unsigned)id]));
    ret = drv_vfd_init(&s_vfd[(unsigned)id],
                       serial_port,
                       baud,
                       modbus_addr,
                       pin_fwd,
                       pin_rev,
                       pin_rst,
                       vfd_do_set);
    if (ret != SW_OK)
    {
        s_vfd_bound[(unsigned)id] = false;
        return ret;
    }

    s_vfd_bound[(unsigned)id] = true;
    return SW_OK;
}

sw_err_t hal_vfd_linux_instance_config_speed_io(hal_vfd_id_t  id,
                                                 io_do_t       pin_spd1,
                                                 io_do_t       pin_spd2,
                                                 const uint8_t spd_cfg[VFD_GEAR_MAX])
{
    drv_vfd_t *vfd = vfd_by_id(id);

    if (vfd == NULL)
    {
        return SW_ERR_PARAM;
    }
    return drv_vfd_config_speed_io(vfd, pin_spd1, pin_spd2, spd_cfg);
}

sw_err_t hal_vfd_linux_instance_run(hal_vfd_id_t id, drv_vfd_gear_t gear)
{
    drv_vfd_t *vfd = vfd_by_id(id);

    if (vfd == NULL)
    {
        return SW_ERR_NOT_INIT;
    }
    return drv_vfd_run(vfd, gear);
}

sw_err_t hal_vfd_linux_instance_set_freq(hal_vfd_id_t id, uint16_t freq_hz)
{
    drv_vfd_t *vfd = vfd_by_id(id);

    if (vfd == NULL)
    {
        return SW_ERR_NOT_INIT;
    }
    return drv_vfd_write(vfd, DRV_VFD_REG_FREQ, freq_hz);
}

void hal_vfd_linux_instance_register_event_cb(hal_vfd_id_t id, void (*cb)(int event_code))
{
    drv_vfd_t *vfd = vfd_by_id(id);

    if (vfd != NULL)
    {
        drv_vfd_register_event_cb(vfd, cb);
    }
}

sw_err_t hal_vfd_linux_instance_set_monitor_mask(hal_vfd_id_t id, drv_vfd_monitor_mask_t mask)
{
    drv_vfd_t *vfd = vfd_by_id(id);

    if (vfd == NULL)
    {
        return SW_ERR_NOT_INIT;
    }
    return drv_vfd_set_monitor_mask(vfd, mask);
}

/* -------------------------------------------------------------------------
 * hal_vfd_ops_t 内部实现（对接 hal_vfd_port 注册机制）
 * ------------------------------------------------------------------------- */
static sw_err_t vfd_init(void)
{
    return SW_OK;
}

static sw_err_t vfd_run(hal_vfd_id_t id, hal_vfd_gear_t gear)
{
    drv_vfd_t *vfd = vfd_by_id(id);

    if (vfd == NULL)
    {
        return SW_ERR_NOT_INIT;
    }
    return drv_vfd_run(vfd, (drv_vfd_gear_t)gear);
}

static sw_err_t vfd_set_freq(hal_vfd_id_t id, uint16_t freq_hz)
{
    drv_vfd_t *vfd = vfd_by_id(id);

    if (vfd == NULL)
    {
        return SW_ERR_NOT_INIT;
    }
    return drv_vfd_write(vfd, DRV_VFD_REG_FREQ, freq_hz);
}

static sw_err_t vfd_stop(hal_vfd_id_t id)
{
    drv_vfd_t *vfd = vfd_by_id(id);

    if (vfd == NULL)
    {
        return SW_ERR_NOT_INIT;
    }
    return drv_vfd_run(vfd, VFD_GEAR_STOP);
}

static sw_err_t vfd_fault_reset(hal_vfd_id_t id)
{
    drv_vfd_t *vfd = vfd_by_id(id);

    if (vfd == NULL)
    {
        return SW_ERR_NOT_INIT;
    }
    return drv_vfd_fault_reset(vfd);
}

static hal_vfd_state_t vfd_get_state(hal_vfd_id_t id)
{
    drv_vfd_t *vfd = vfd_by_id(id);

    if (vfd == NULL)
    {
        return HAL_VFD_STATE_STOPPED;
    }
    return drv_vfd_get_state(vfd);
}

static sw_err_t vfd_read(hal_vfd_id_t id, hal_vfd_reg_t reg, uint16_t *p_val)
{
    drv_vfd_t *vfd = vfd_by_id(id);

    if ((vfd == NULL) || (p_val == NULL))
    {
        return SW_ERR_NOT_INIT;
    }
    return drv_vfd_read(vfd, (drv_vfd_reg_t)reg, p_val);
}

static sw_err_t vfd_get_cached(hal_vfd_id_t id, hal_vfd_reg_t reg, uint16_t *p_val)
{
    drv_vfd_t *vfd = vfd_by_id(id);

    if ((vfd == NULL) || (p_val == NULL))
    {
        return SW_ERR_NOT_INIT;
    }
    *p_val = drv_vfd_get_cached(vfd, (drv_vfd_reg_t)reg);
    return SW_OK;
}

static void vfd_register_event_cb(hal_vfd_id_t id, void (*cb)(int event_code))
{
    hal_vfd_linux_instance_register_event_cb(id, cb);
}

static const hal_vfd_ops_t s_ops = {
    .init               = vfd_init,
    .run                = vfd_run,
    .set_freq           = vfd_set_freq,
    .stop               = vfd_stop,
    .fault_reset        = vfd_fault_reset,
    .get_state          = vfd_get_state,
    .read               = vfd_read,
    .get_cached         = vfd_get_cached,
    .register_event_cb  = vfd_register_event_cb,
};

void hal_vfd_linux_register(void)
{
    hal_vfd_register(&s_ops);
}
