/**
 * @file    hal_water_sim.c
 * @brief   水路控制 HAL 仿真实现
 * @author  胡望伟
 * @date    2026-04-10
 */

#include "ports/hal/hal_water_port.h"
#include "common/log.h"

static sw_err_t sim_pump_set(bool on)
{
    LOG_INFO("sim_water: pump %s", on ? "ON" : "OFF");
    return SW_OK;
}

static sw_err_t sim_curtain_set(bool on)
{
    LOG_INFO("sim_water: curtain %s", on ? "ON" : "OFF");
    return SW_OK;
}

static sw_err_t sim_foam_set(bool on)
{
    LOG_INFO("sim_water: foam %s", on ? "ON" : "OFF");
    return SW_OK;
}

static sw_err_t sim_brush_water_set(bool on)
{
    LOG_INFO("sim_water: brush_water %s", on ? "ON" : "OFF");
    return SW_OK;
}

static sw_err_t sim_highpres_set(bool on)
{
    LOG_INFO("sim_water: highpres %s", on ? "ON" : "OFF");
    return SW_OK;
}

static sw_err_t sim_all_off(void)
{
    LOG_INFO("sim_water: all OFF");
    return SW_OK;
}

static const hal_water_ops_t s_ops = {
    .pump_set        = sim_pump_set,
    .curtain_set     = sim_curtain_set,
    .foam_set        = sim_foam_set,
    .brush_water_set = sim_brush_water_set,
    .highpres_set    = sim_highpres_set,
    .all_off         = sim_all_off,
};

void hal_water_sim_register(void)
{
    hal_water_register(&s_ops);
    LOG_INFO("hal_water_sim: registered");
}
