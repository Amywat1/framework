/**
 * @file    wash_mode.c
 * @brief   洗车模式管理实现
 * @author  胡望伟
 * @date    2026-04-10
 */

#include "domain/process/wash_mode.h"
#include "common/log.h"

static wash_mode_t s_mode = WASH_MODE_STANDARD;

sw_err_t wash_mode_init(void)
{
    /*
     * 当前使用编译期默认模式 `WASH_MODE_STANDARD`。
     */
    s_mode = WASH_MODE_STANDARD;
    LOG_INFO("wash_mode: init ok, default=%d", (int)s_mode);
    return SW_OK;
}

wash_mode_t wash_mode_get(void)
{
    return s_mode;
}

sw_err_t wash_mode_set(wash_mode_t mode)
{
    if (mode >= WASH_MODE_MAX)
    {
        return SW_ERR_PARAM;
    }
    s_mode = mode;
    LOG_INFO("wash_mode: set to %d", (int)mode);
    return SW_OK;
}
