/**
 * @file    gate.c
 * @brief   入口挡杆与指示灯设备实现
 * @author  胡望伟
 * @date    2026-04-10
 */

#include "domain/device/gate.h"
#include "ports/hal/hal_indicator_port.h"
#include "common/log.h"

sw_err_t gate_init(void)
{
    const hal_indicator_ops_t *ops = hal_indicator_get_ops();
    sw_err_t                   ret;

    ret = ops->rod_close();
    if (ret != SW_OK)
    {
        LOG_ERROR("gate_init: rod_close failed ret=%d", (int)ret);
        return ret;
    }
    ret = ops->entry_light_set(HAL_LIGHT_RED);
    if (ret != SW_OK)
    {
        LOG_ERROR("gate_init: entry_light_set failed ret=%d", (int)ret);
        return ret;
    }

    LOG_INFO("gate: init ok (blocked, red light)");
    return SW_OK;
}

sw_err_t gate_allow(void)
{
    const hal_indicator_ops_t *ops = hal_indicator_get_ops();
    sw_err_t                   ret;

    ret = ops->rod_open();
    if (ret != SW_OK)
    {
        LOG_ERROR("gate_allow: rod_open failed ret=%d", (int)ret);
        return ret;
    }
    ret = ops->entry_light_set(HAL_LIGHT_GREEN);
    if (ret != SW_OK)
    {
        LOG_ERROR("gate_allow: entry_light_set failed ret=%d", (int)ret);
        return ret;
    }

    LOG_INFO("gate: allow");
    return SW_OK;
}

sw_err_t gate_block(void)
{
    const hal_indicator_ops_t *ops = hal_indicator_get_ops();
    sw_err_t                   ret;

    ret = ops->rod_close();
    if (ret != SW_OK)
    {
        LOG_ERROR("gate_block: rod_close failed ret=%d", (int)ret);
        return ret;
    }
    ret = ops->entry_light_set(HAL_LIGHT_RED);
    if (ret != SW_OK)
    {
        LOG_ERROR("gate_block: entry_light_set failed ret=%d", (int)ret);
        return ret;
    }

    LOG_INFO("gate: block");
    return SW_OK;
}

sw_err_t gate_set_light(hal_light_state_t state)
{
    return hal_indicator_get_ops()->entry_light_set(state);
}
