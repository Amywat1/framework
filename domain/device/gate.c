/**
 * @file    gate.c
 * @brief   入口挡杆与指示灯设备实现
 * @author  胡望伟
 * @date    2026-04-10
 */

#include "domain/device/gate.h"
#include "common/log.h"

sw_err_t gate_init(void)
{
    LOG_INFO("gate: init ok (no hardware)");
    return SW_OK;
}

sw_err_t gate_allow(void)
{
    LOG_INFO("gate: allow (no hardware)");
    return SW_OK;
}

sw_err_t gate_block(void)
{
    LOG_INFO("gate: block (no hardware)");
    return SW_OK;
}

sw_err_t gate_set_light(gate_light_state_t state)
{
    (void)state;
    return SW_OK;
}
