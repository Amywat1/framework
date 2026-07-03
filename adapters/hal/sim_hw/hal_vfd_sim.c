/**
 * @file    hal_vfd_sim.c
 * @brief   变频器 HAL 仿真实现
 * @author  HUWANGWEI
 * @date    2026-06-01
 */

#include "ports/hal/hal_vfd_port.h"
#include "machines/m8/config/m8_vfd_table.h"
#include "common/log.h"

#include <stddef.h>

static hal_vfd_state_t s_state[HAL_VFD_ID_MAX];

static sw_err_t sim_vfd_init(void)
{
    s_state[HAL_VFD_GANTRY] = HAL_VFD_STATE_STOPPED;
    s_state[HAL_VFD_BRUSH]  = HAL_VFD_STATE_STOPPED;
    s_state[HAL_VFD_FAN]    = HAL_VFD_STATE_STOPPED;
    LOG_INFO("hal_vfd_sim: init ok");
    return SW_OK;
}

static bool sim_id_valid(hal_vfd_id_t id)
{
    return (id == HAL_VFD_GANTRY) || (id == HAL_VFD_BRUSH) || (id == HAL_VFD_FAN);
}

static sw_err_t sim_run(hal_vfd_id_t id, hal_vfd_gear_t gear)
{
    if (!sim_id_valid(id))
    {
        return SW_ERR_PARAM;
    }
    if ((gear < 0) && (id != HAL_VFD_GANTRY))
    {
        return SW_ERR_PARAM;
    }
    if (gear > 0)
    {
        s_state[id] = HAL_VFD_STATE_FWD;
    }
    else if (gear < 0)
    {
        s_state[id] = HAL_VFD_STATE_REV;
    }
    else
    {
        s_state[id] = HAL_VFD_STATE_STOPPED;
    }
    return SW_OK;
}

static sw_err_t sim_set_freq(hal_vfd_id_t id, uint16_t freq_hz)
{
    if (!sim_id_valid(id))
    {
        return SW_ERR_PARAM;
    }
    (void)freq_hz;
    return SW_OK;
}

static sw_err_t sim_stop(hal_vfd_id_t id)
{
    if (!sim_id_valid(id))
    {
        return SW_ERR_PARAM;
    }
    s_state[id] = HAL_VFD_STATE_STOPPED;
    return SW_OK;
}

static sw_err_t sim_fault_reset(hal_vfd_id_t id)
{
    if (!sim_id_valid(id))
    {
        return SW_ERR_PARAM;
    }
    s_state[id] = HAL_VFD_STATE_STOPPED;
    return SW_OK;
}

static hal_vfd_state_t sim_get_state(hal_vfd_id_t id)
{
    if (!sim_id_valid(id))
    {
        return HAL_VFD_STATE_STOPPED;
    }
    return s_state[id];
}

static sw_err_t sim_read(hal_vfd_id_t id, hal_vfd_reg_t reg, uint16_t *p_val)
{
    if (!sim_id_valid(id) || (p_val == NULL))
    {
        return SW_ERR_PARAM;
    }
    (void)reg;
    *p_val = 0U;
    return SW_OK;
}

static sw_err_t sim_get_cached(hal_vfd_id_t id, hal_vfd_reg_t reg, uint16_t *p_val)
{
    if (!sim_id_valid(id) || (p_val == NULL))
    {
        return SW_ERR_PARAM;
    }
    (void)reg;
    *p_val = 0U;
    return SW_OK;
}

static void sim_register_event_cb(hal_vfd_id_t id, void (*cb)(int event_code))
{
    (void)id;
    (void)cb;
}

static const hal_vfd_ops_t s_ops = {
    .init               = sim_vfd_init,
    .run                = sim_run,
    .set_freq           = sim_set_freq,
    .stop               = sim_stop,
    .fault_reset        = sim_fault_reset,
    .get_state          = sim_get_state,
    .read               = sim_read,
    .get_cached         = sim_get_cached,
    .register_event_cb  = sim_register_event_cb,
};

void hal_vfd_sim_register(void)
{
    hal_vfd_register(&s_ops);
    LOG_INFO("hal_vfd_sim: registered");
}
