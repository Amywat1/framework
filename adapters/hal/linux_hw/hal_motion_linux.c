/**
 * @file    hal_motion_linux.c
 * @brief   运动控制 HAL 端口 — Linux 真机实现（龙门/刷子）
 * @author  胡望伟
 * @date    2026-04-10
 */

#include "ports/hal/hal_motion_port.h"
#include "adapters/hal/linux_hw/m8_vfd_control.h"
#include "adapters/machine/m8/m8_machine_map.h"
#include "ports/hal/hal_io_port.h"
#include "common/log.h"
#include "common/sw_error.h"

#include <unistd.h>  /* usleep（接触器等待，非精度关键路径）*/

static sw_err_t io_do_set(io_do_t pin, bool val)
{
    const hal_io_ops_t *ops = hal_io_get_ops();

    if ((ops == NULL) || (ops->do_set == NULL))
    {
        return SW_ERR_NOT_INIT;
    }
    return ops->do_set(pin, val);
}

/* -------------------------------------------------------------------------
 * 龙门 VFD
 * ------------------------------------------------------------------------- */
static sw_err_t m8_gantry_fwd(uint16_t freq_hz)
{
    return m8_vfd_gantry_set_speed((int)freq_hz);
}

static sw_err_t m8_gantry_rev(uint16_t freq_hz)
{
    return m8_vfd_gantry_set_speed(-(int)freq_hz);
}

static sw_err_t m8_gantry_stop(void)
{
    return m8_vfd_gantry_set_speed(0);
}

static sw_err_t m8_gantry_fault_reset(void)
{
    return m8_vfd_gantry_fault_reset();
}

/* -------------------------------------------------------------------------
 * 刷子 VFD + 接触器
 * ------------------------------------------------------------------------- */
static sw_err_t m8_brush_select(hal_brush_sel_t sel)
{
    if (m8_vfd_brush_is_running())
    {
        LOG_ERROR("hal_motion: brush_select called while VFD running");
        return SW_ERR_STATE;
    }

    /* 先断开全部接触器，等 200ms 防止同时吸合 */
    (void)io_do_set(M8_DO_TOP_BRUSH_ACT,  false);
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
    return m8_vfd_brush_set_speed((int)freq_hz);
}

static sw_err_t m8_brush_stop(void)
{
    return m8_vfd_brush_set_speed(0);
}

static sw_err_t m8_brush_fault_reset(void)
{
    return m8_vfd_brush_fault_reset();
}

/* -------------------------------------------------------------------------
 * 操作表 + 注册
 * ------------------------------------------------------------------------- */
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
