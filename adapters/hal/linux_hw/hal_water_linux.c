/**
 * @file    hal_water_linux.c
 * @brief   水路控制 HAL 端口 — Linux 真机实现
 * @author  胡望伟
 * @date    2026-04-10
 */

#include "ports/hal/hal_water_port.h"
#include "adapters/machine/m8/m8_machine_map.h"
#include "ports/hal/hal_io_port.h"

static sw_err_t io_do_set(io_do_t pin, bool on)
{
    const hal_io_ops_t *ops = hal_io_get_ops();

    if ((ops == NULL) || (ops->do_set == NULL))
    {
        return SW_ERR_NOT_INIT;
    }
    return ops->do_set(pin, on);
}

static sw_err_t m8_pump_set(bool on)        { return io_do_set(M8_DO_WATER_PUMP,     on); }
static sw_err_t m8_curtain_set(bool on)     { return io_do_set(M8_DO_WATER_CURTAIN,  on); }
static sw_err_t m8_foam_set(bool on)        { return io_do_set(M8_DO_WATER_FOAM,     on); }
static sw_err_t m8_brush_water_set(bool on) { return io_do_set(M8_DO_WATER_BRUSH,    on); }
static sw_err_t m8_highpres_set(bool on)    { return io_do_set(M8_DO_WATER_HIGHPRES, on); }

static sw_err_t m8_all_off(void)
{
    (void)io_do_set(M8_DO_WATER_PUMP,     false);
    (void)io_do_set(M8_DO_WATER_CURTAIN,  false);
    (void)io_do_set(M8_DO_WATER_FOAM,     false);
    (void)io_do_set(M8_DO_WATER_BRUSH,    false);
    (void)io_do_set(M8_DO_WATER_HIGHPRES, false);
    return SW_OK;
}

static const hal_water_ops_t s_ops = {
    .pump_set        = m8_pump_set,
    .curtain_set     = m8_curtain_set,
    .foam_set        = m8_foam_set,
    .brush_water_set = m8_brush_water_set,
    .highpres_set    = m8_highpres_set,
    .all_off         = m8_all_off,
};

void hal_water_linux_register(void)
{
    hal_water_register(&s_ops);
}
