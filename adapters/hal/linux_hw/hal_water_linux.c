/**
 * @file    hal_water_linux.c
 * @brief   水路控制 HAL 端口 — Linux 真机实现
 * @author  胡望伟
 * @date    2026-04-10
 */

#include "ports/hal/hal_water_port.h"
#include "adapters/machine/m8/m8_machine_map.h"
#include "adapters/hal/linux_hw/drv/drv_io.h"

static sw_err_t m8_pump_set(bool on)        { return drv_io_do_set(M8_DO_WATER_PUMP,     on); }
static sw_err_t m8_curtain_set(bool on)     { return drv_io_do_set(M8_DO_WATER_CURTAIN,  on); }
static sw_err_t m8_foam_set(bool on)        { return drv_io_do_set(M8_DO_WATER_FOAM,     on); }
static sw_err_t m8_brush_water_set(bool on) { return drv_io_do_set(M8_DO_WATER_BRUSH,    on); }
static sw_err_t m8_highpres_set(bool on)    { return drv_io_do_set(M8_DO_WATER_HIGHPRES, on); }

static sw_err_t m8_all_off(void)
{
    (void)drv_io_do_set(M8_DO_WATER_PUMP,     false);
    (void)drv_io_do_set(M8_DO_WATER_CURTAIN,  false);
    (void)drv_io_do_set(M8_DO_WATER_FOAM,     false);
    (void)drv_io_do_set(M8_DO_WATER_BRUSH,    false);
    (void)drv_io_do_set(M8_DO_WATER_HIGHPRES, false);
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
