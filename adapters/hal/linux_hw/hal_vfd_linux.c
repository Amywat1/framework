/**
 * @file    hal_vfd_linux.c
 * @brief   Linux 真机 VFD HAL：drv_vfd backend + generic/hal_vfd 组合层
 * @author  HUWANGWEI
 * @date    2026-06-01
 */

#include "adapters/hal/linux_hw/hal_vfd_linux.h"

#include "adapters/hal/generic/hal_vfd.h"
#include "adapters/hal/linux_hw/drv/drv_io.h"
#include "adapters/hal/linux_hw/drv/drv_vfd.h"

#include <string.h>

#define VFD_LINUX_SLOT_COUNT  8U

static drv_vfd_t  s_vfd[VFD_LINUX_SLOT_COUNT];
static bool       s_drv_inited[VFD_LINUX_SLOT_COUNT];

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
    if (!vfd_id_valid(id) || !s_drv_inited[(unsigned)id])
    {
        return NULL;
    }
    return &s_vfd[(unsigned)id];
}

static sw_err_t backend_apply_gear(void *ctx, hal_vfd_gear_t gear)
{
    return drv_vfd_apply_gear((drv_vfd_t *)ctx, (drv_vfd_gear_t)gear);
}

static sw_err_t backend_stop_outputs(void *ctx)
{
    return drv_vfd_stop_outputs((drv_vfd_t *)ctx);
}

static sw_err_t backend_set_rst(void *ctx, bool level)
{
    return drv_vfd_set_rst((drv_vfd_t *)ctx, level);
}

static sw_err_t backend_read(void *ctx, hal_vfd_reg_t reg, uint16_t *p_val)
{
    return drv_vfd_read((drv_vfd_t *)ctx, (drv_vfd_reg_t)reg, p_val);
}

static sw_err_t backend_write(void *ctx, hal_vfd_reg_t reg, uint16_t val)
{
    return drv_vfd_write((drv_vfd_t *)ctx, (drv_vfd_reg_t)reg, val);
}

static hal_vfd_state_t backend_get_state(void *ctx)
{
    return drv_vfd_get_state((drv_vfd_t *)ctx);
}

static bool backend_has_rst_pin(void *ctx)
{
    drv_vfd_t *vfd = (drv_vfd_t *)ctx;

    if (vfd == NULL)
    {
        return false;
    }
    return vfd->pin_rst.raw != IO_HANDLE_NULL;
}

static sw_err_t linux_bind_instance(hal_vfd_id_t id, drv_vfd_t *vfd,
                                    hal_vfd_monitor_mask_t mask)
{
    hal_vfd_bind_cfg_t cfg;

    cfg.drv_ctx            = vfd;
    cfg.apply_gear         = backend_apply_gear;
    cfg.stop_outputs       = backend_stop_outputs;
    cfg.set_rst            = backend_set_rst;
    cfg.read               = backend_read;
    cfg.write              = backend_write;
    cfg.get_state          = backend_get_state;
    cfg.has_rst_pin        = backend_has_rst_pin;
    cfg.rst_pulse_ms       = HAL_VFD_DEFAULT_RST_PULSE_MS;
    cfg.monitor_period_ms  = HAL_VFD_DEFAULT_MONITOR_PERIOD_MS;
    cfg.monitor_mask       = mask;
    return hal_vfd_bind(id, &cfg);
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
        s_drv_inited[(unsigned)id] = false;
        return ret;
    }

    ret = linux_bind_instance(id, &s_vfd[(unsigned)id], HAL_VFD_MON_ALL);
    if (ret != SW_OK)
    {
        s_drv_inited[(unsigned)id] = false;
        return ret;
    }

    s_drv_inited[(unsigned)id] = true;
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

sw_err_t hal_vfd_linux_instance_set_monitor_mask(hal_vfd_id_t id,
                                                  hal_vfd_monitor_mask_t mask)
{
    if (!vfd_id_valid(id) || !s_drv_inited[(unsigned)id])
    {
        return SW_ERR_NOT_INIT;
    }
    return hal_vfd_set_monitor_mask(id, mask);
}

void hal_vfd_linux_register(void)
{
    hal_vfd_generic_register();
}
