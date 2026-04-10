/**
 * @file    water.c
 * @brief   水路设备实现
 * @author  胡望伟
 * @date    2026-04-10
 */

#include "domain/device/water.h"
#include "ports/hal/hal_water_port.h"
#include "common/log.h"

sw_err_t water_init(void)
{
    return water_all_off();
}

sw_err_t water_prewash_on(void)
{
    const hal_water_ops_t *ops = hal_water_get_ops();
    (void)ops->pump_set(true);
    (void)ops->foam_set(true);
    (void)ops->curtain_set(true);
    LOG_INFO("water: prewash ON");
    return SW_OK;
}

sw_err_t water_prewash_off(void)
{
    const hal_water_ops_t *ops = hal_water_get_ops();
    (void)ops->foam_set(false);
    (void)ops->curtain_set(false);
    LOG_INFO("water: prewash OFF");
    return SW_OK;
}

sw_err_t water_brush_on(void)
{
    const hal_water_ops_t *ops = hal_water_get_ops();
    (void)ops->pump_set(true);
    (void)ops->brush_water_set(true);
    return SW_OK;
}

sw_err_t water_brush_off(void)
{
    const hal_water_ops_t *ops = hal_water_get_ops();
    (void)ops->brush_water_set(false);
    return SW_OK;
}

sw_err_t water_highpres_on(void)
{
    const hal_water_ops_t *ops = hal_water_get_ops();
    (void)ops->pump_set(true);
    (void)ops->highpres_set(true);
    LOG_INFO("water: high pressure ON");
    return SW_OK;
}

sw_err_t water_highpres_off(void)
{
    const hal_water_ops_t *ops = hal_water_get_ops();
    (void)ops->highpres_set(false);
    return SW_OK;
}

sw_err_t water_all_off(void)
{
    const hal_water_ops_t *ops = hal_water_get_ops();
    (void)ops->all_off();
    LOG_INFO("water: all OFF");
    return SW_OK;
}
