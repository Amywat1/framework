/**
 * @file    hal_vfd_linux.c
 * @brief   变频器 HAL 端口 Linux 真机实现（通用 drv_vfd 转发）
 * @author  胡望伟
 * @date    2026-06-01
 */

#include "adapters/hal/linux_hw/hal_vfd_linux.h"
#include "ports/hal/hal_vfd_port.h"
#include "ports/hal/hal_io_port.h"
#include "adapters/hal/linux_hw/drv/drv_vfd.h"

#include <string.h>

#define VFD_LINUX_SLOT_COUNT    8U

static drv_vfd_t  s_vfd[VFD_LINUX_SLOT_COUNT];
static bool       s_vfd_bound[VFD_LINUX_SLOT_COUNT];

static sw_err_t vfd_do_set(io_do_t pin, bool val)
{
    const hal_io_ops_t *ops = hal_io_get_ops();

    if ((ops == NULL) || (ops->do_set == NULL))
    {
        return SW_ERR_NOT_INIT;
    }
    return ops->do_set(pin, val);
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
                                     bool          has_rev,
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
                       has_rev,
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

void hal_vfd_linux_instance_register_event_cb(hal_vfd_id_t id, void (*cb)(int event_code))
{
    drv_vfd_t *vfd = vfd_by_id(id);

    if (vfd != NULL)
    {
        drv_vfd_register_event_cb(vfd, cb);
    }
}

static sw_err_t vfd_init(void)
{
    return SW_OK;
}

static sw_err_t vfd_run_fwd(hal_vfd_id_t id, uint16_t freq_hz)
{
    drv_vfd_t *vfd = vfd_by_id(id);

    if (vfd == NULL)
    {
        return SW_ERR_NOT_INIT;
    }
    return drv_vfd_run_fwd(vfd, freq_hz);
}

static sw_err_t vfd_run_rev(hal_vfd_id_t id, uint16_t freq_hz)
{
    drv_vfd_t *vfd = vfd_by_id(id);

    if (vfd == NULL)
    {
        return SW_ERR_NOT_INIT;
    }
    return drv_vfd_run_rev(vfd, freq_hz);
}

static sw_err_t vfd_stop(hal_vfd_id_t id)
{
    drv_vfd_t *vfd = vfd_by_id(id);

    if (vfd == NULL)
    {
        return SW_ERR_NOT_INIT;
    }
    return drv_vfd_stop(vfd);
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

/* 返回 monitor worker 维护的缓存值，无 Modbus IO，不阻塞调用方 */
static sw_err_t vfd_get_fault_code(hal_vfd_id_t id, uint16_t *p_code)
{
    drv_vfd_t *vfd = vfd_by_id(id);

    if ((vfd == NULL) || (p_code == NULL))
    {
        return SW_ERR_NOT_INIT;
    }
    *p_code = drv_vfd_get_cached_fault_code(vfd);
    return SW_OK;
}

/* 返回 monitor worker 维护的缓存值，无 Modbus IO，不阻塞调用方 */
static sw_err_t vfd_read_current(hal_vfd_id_t id, uint16_t *p_current)
{
    drv_vfd_t *vfd = vfd_by_id(id);

    if ((vfd == NULL) || (p_current == NULL))
    {
        return SW_ERR_NOT_INIT;
    }
    *p_current = drv_vfd_get_cached_current(vfd);
    return SW_OK;
}

static sw_err_t vfd_read_status(hal_vfd_id_t id, uint16_t *p_status)
{
    drv_vfd_t *vfd = vfd_by_id(id);

    if (vfd == NULL)
    {
        return SW_ERR_NOT_INIT;
    }
    return drv_vfd_read_status(vfd, p_status);
}

static void vfd_register_event_cb(hal_vfd_id_t id, void (*cb)(int event_code))
{
    hal_vfd_linux_instance_register_event_cb(id, cb);
}

static const hal_vfd_ops_t s_ops = {
    .init               = vfd_init,
    .run_fwd            = vfd_run_fwd,
    .run_rev            = vfd_run_rev,
    .stop               = vfd_stop,
    .fault_reset        = vfd_fault_reset,
    .get_state          = vfd_get_state,
    .get_fault_code     = vfd_get_fault_code,
    .read_current       = vfd_read_current,
    .read_status        = vfd_read_status,
    .register_event_cb  = vfd_register_event_cb,
};

void hal_vfd_linux_register(void)
{
    hal_vfd_register(&s_ops);
}
