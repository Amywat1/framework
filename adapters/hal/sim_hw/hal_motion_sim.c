/**
 * @file    hal_motion_sim.c
 * @brief   运动控制 HAL 仿真实现
 * @author  胡望伟
 * @date    2026-04-10
 */

#include "ports/hal/hal_motion_port.h"
#include "core/event_bus/event_bus.h"
#include "common/event_types.h"
#include "common/log.h"

static sw_err_t sim_gantry_fwd(uint16_t freq_hz)
{
    LOG_INFO("sim_motion: gantry_fwd freq=%u", (unsigned)freq_hz);
    return SW_OK;
}

static sw_err_t sim_gantry_rev(uint16_t freq_hz)
{
    LOG_INFO("sim_motion: gantry_rev freq=%u", (unsigned)freq_hz);
    return SW_OK;
}

static sw_err_t sim_gantry_stop(void)
{
    LOG_INFO("sim_motion: gantry_stop");
    return SW_OK;
}

static sw_err_t sim_gantry_fault_reset(void)
{
    LOG_INFO("sim_motion: gantry_fault_reset");
    return SW_OK;
}

static sw_err_t sim_brush_select(hal_brush_sel_t sel)
{
    LOG_INFO("sim_motion: brush_select sel=%d", (int)sel);
    return SW_OK;
}

static sw_err_t sim_brush_run(uint16_t freq_hz)
{
    LOG_INFO("sim_motion: brush_run freq=%u", (unsigned)freq_hz);
    return SW_OK;
}

static sw_err_t sim_brush_stop(void)
{
    LOG_INFO("sim_motion: brush_stop");
    return SW_OK;
}

static sw_err_t sim_brush_fault_reset(void)
{
    LOG_INFO("sim_motion: brush_fault_reset");
    return SW_OK;
}

static sw_err_t sim_lift_up_start(uint32_t pulses)
{
    LOG_INFO("sim_motion: lift_up_start pulses=%u", (unsigned)pulses);
    /* 仿真中立即完成 */
    (void)event_publish(EVT_COMP_LIFT_DONE, (uint32_t)SW_OK);
    return SW_OK;
}

static sw_err_t sim_lift_down_start(uint32_t pulses)
{
    LOG_INFO("sim_motion: lift_down_start pulses=%u", (unsigned)pulses);
    (void)event_publish(EVT_COMP_LIFT_DONE, (uint32_t)SW_OK);
    return SW_OK;
}

static const hal_motion_ops_t s_ops = {
    .gantry_fwd         = sim_gantry_fwd,
    .gantry_rev         = sim_gantry_rev,
    .gantry_stop        = sim_gantry_stop,
    .gantry_fault_reset = sim_gantry_fault_reset,
    .brush_select       = sim_brush_select,
    .brush_run          = sim_brush_run,
    .brush_stop         = sim_brush_stop,
    .brush_fault_reset  = sim_brush_fault_reset,
    .lift_up_start      = sim_lift_up_start,
    .lift_down_start    = sim_lift_down_start,
};

void hal_motion_sim_register(void)
{
    hal_motion_register(&s_ops);
    LOG_INFO("hal_motion_sim: registered");
}
