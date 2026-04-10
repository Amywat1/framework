/**
 * @file    comp_water.c
 * @brief   水路组件实现
 * @author  HUWANGWEI
 * @date    2026-04-07
 */

#include "comp_water.h"
#include "bsp/bsp_hal.h"
#include "common/log.h"

sw_err_t comp_water_init(void)
{
    return comp_water_all_off();
}

sw_err_t comp_water_prewash_on(void)
{
    (void)hal_water_pump_set(true);
    (void)hal_water_foam_set(true);
    (void)hal_water_curtain_set(true);
    LOG_INFO("comp_water: prewash ON");
    return SW_OK;
}

sw_err_t comp_water_prewash_off(void)
{
    (void)hal_water_foam_set(false);
    (void)hal_water_curtain_set(false);
    LOG_INFO("comp_water: prewash OFF");
    return SW_OK;
}

sw_err_t comp_water_brush_on(void)
{
    (void)hal_water_pump_set(true);
    (void)hal_water_brush_set(true);
    return SW_OK;
}

sw_err_t comp_water_brush_off(void)
{
    (void)hal_water_brush_set(false);
    return SW_OK;
}

sw_err_t comp_water_highpres_on(void)
{
    (void)hal_water_pump_set(true);
    (void)hal_water_highpres_set(true);
    LOG_INFO("comp_water: high pressure ON");
    return SW_OK;
}

sw_err_t comp_water_highpres_off(void)
{
    (void)hal_water_highpres_set(false);
    return SW_OK;
}

sw_err_t comp_water_all_off(void)
{
    (void)hal_water_pump_set(false);
    (void)hal_water_curtain_set(false);
    (void)hal_water_foam_set(false);
    (void)hal_water_brush_set(false);
    (void)hal_water_highpres_set(false);
    LOG_INFO("comp_water: all OFF");
    return SW_OK;
}
