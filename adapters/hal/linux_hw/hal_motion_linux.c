/**
 * @file    hal_motion_linux.c
 * @brief   运动控制 HAL 端口 — Linux 真机实现（龙门/刷子）
 * @author  胡望伟
 * @date    2026-04-10
 */

#include "ports/hal/hal_motion_port.h"
#include "ports/hal/hal_vfd_port.h"
#include "ports/hal/hal_io_port.h"
#include "adapters/machine/m8/m8_machine_map.h"
#include "common/log.h"
#include "common/sw_error.h"

#include <unistd.h>

static sw_err_t io_do_set(io_do_t pin, bool val)
{
    const hal_io_ops_t *ops = hal_io_get_ops();

    if ((ops == NULL) || (ops->do_set == NULL))
    {
        return SW_ERR_NOT_INIT;
    }
    return ops->do_set(pin, val);
}

static sw_err_t vfd_set_speed(hal_vfd_id_t id, int speed_ref)
{
    const hal_vfd_ops_t *vfd = hal_vfd_get_ops();

    if (vfd == NULL)
    {
        return SW_ERR_NOT_INIT;
    }
    if (speed_ref > 0)
    {
        if (vfd->run_fwd == NULL)
        {
            return SW_ERR_NOT_INIT;
        }
        return vfd->run_fwd(id, (uint16_t)speed_ref);
    }
    if (speed_ref < 0)
    {
        if (vfd->run_rev == NULL)
        {
            return SW_ERR_NOT_INIT;
        }
        return vfd->run_rev(id, (uint16_t)(-speed_ref));
    }
    if (vfd->stop == NULL)
    {
        return SW_ERR_NOT_INIT;
    }
    return vfd->stop(id);
}

static sw_err_t m8_gantry_fwd(uint16_t freq_hz)
{
    return vfd_set_speed(HAL_VFD_GANTRY, (int)freq_hz);
}

static sw_err_t m8_gantry_rev(uint16_t freq_hz)
{
    return vfd_set_speed(HAL_VFD_GANTRY, -(int)freq_hz);
}

static sw_err_t m8_gantry_stop(void)
{
    return vfd_set_speed(HAL_VFD_GANTRY, 0);
}

static sw_err_t m8_gantry_fault_reset(void)
{
    const hal_vfd_ops_t *vfd = hal_vfd_get_ops();

    if ((vfd == NULL) || (vfd->fault_reset == NULL))
    {
        return SW_ERR_NOT_INIT;
    }
    return vfd->fault_reset(HAL_VFD_GANTRY);
}

static sw_err_t m8_brush_select(hal_brush_sel_t sel)
{
    const hal_vfd_ops_t *vfd = hal_vfd_get_ops();

    if ((vfd != NULL) && (vfd->get_state != NULL) &&
        (vfd->get_state(HAL_VFD_BRUSH) == HAL_VFD_STATE_FWD))
    {
        LOG_ERROR("hal_motion: brush_select called while VFD running");
        return SW_ERR_STATE;
    }

    (void)io_do_set(M8_DO_TOP_BRUSH_ACT, false);
    (void)io_do_set(M8_DO_SIDE_BRUSH_ACT, false);
    usleep((unsigned long)M8_BRUSH_CONTACTOR_WAIT_MS * 1000UL);

    if (sel == HAL_BRUSH_TOP)
    {
        (void)io_do_set(M8_DO_TOP_BRUSH_ACT, true);
    }
    else
    {
        (void)io_do_set(M8_DO_SIDE_BRUSH_ACT, true);
    }

    LOG_INFO("hal_motion: brush_select sel=%d", (int)sel);
    return SW_OK;
}

static sw_err_t m8_brush_run(uint16_t freq_hz)
{
    return vfd_set_speed(HAL_VFD_BRUSH, (int)freq_hz);
}

static sw_err_t m8_brush_stop(void)
{
    return vfd_set_speed(HAL_VFD_BRUSH, 0);
}

static sw_err_t m8_brush_fault_reset(void)
{
    const hal_vfd_ops_t *vfd = hal_vfd_get_ops();

    if ((vfd == NULL) || (vfd->fault_reset == NULL))
    {
        return SW_ERR_NOT_INIT;
    }
    return vfd->fault_reset(HAL_VFD_BRUSH);
}

static const hal_motion_ops_t s_ops = {
    .gantry_fwd         = m8_gantry_fwd,
    .gantry_rev         = m8_gantry_rev,
    .gantry_stop        = m8_gantry_stop,
    .gantry_fault_reset = m8_gantry_fault_reset,
    .brush_select       = m8_brush_select,
    .brush_run          = m8_brush_run,
    .brush_stop         = m8_brush_stop,
    .brush_fault_reset  = m8_brush_fault_reset,
};

void hal_motion_linux_register(void)
{
    hal_motion_register(&s_ops);
}
