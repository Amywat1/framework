/**
 * @file    hal_indicator_sim.c
 * @brief   入口指示灯与挡杆 HAL 仿真实现
 * @author  胡望伟
 * @date    2026-04-10
 */

#include "ports/hal/hal_indicator_port.h"
#include "common/log.h"

static const char *light_name(hal_light_state_t s)
{
    switch (s) {
        case HAL_LIGHT_OFF:    return "OFF";
        case HAL_LIGHT_GREEN:  return "GREEN";
        case HAL_LIGHT_RED:    return "RED";
        case HAL_LIGHT_YELLOW: return "YELLOW";
        default:               return "?";
    }
}

static sw_err_t sim_entry_light_set(hal_light_state_t state)
{
    LOG_INFO("sim_indicator: entry_light = %s", light_name(state));
    return SW_OK;
}

static sw_err_t sim_rod_open(void)
{
    LOG_INFO("sim_indicator: rod OPEN (放行)");
    return SW_OK;
}

static sw_err_t sim_rod_close(void)
{
    LOG_INFO("sim_indicator: rod CLOSE (拦截)");
    return SW_OK;
}

static const hal_indicator_ops_t s_ops = {
    .entry_light_set = sim_entry_light_set,
    .rod_open        = sim_rod_open,
    .rod_close       = sim_rod_close,
};

void hal_indicator_sim_register(void)
{
    hal_indicator_register(&s_ops);
    LOG_INFO("hal_indicator_sim: registered");
}
