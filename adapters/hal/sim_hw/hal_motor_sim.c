/**
 * @file    hal_motor_sim.c
 * @brief   通用电机 HAL 端口仿真实现
 * @author  HUWANGWEI
 * @date    2026-04-13
 */

#include "ports/hal/hal_motor_port.h"
#include "adapters/machine/m8/m8_signal_filter.h"
#include "config/machine/m8_signal_table.h"
#include "adapters/hal/sim_hw/sim_encoder_counter.h"
#include "common/log.h"

#define SIM_MOTOR_GANTRY_ID    0
#define SIM_MOTOR_SLOT_COUNT   1

static int s_speed_ref[SIM_MOTOR_SLOT_COUNT];

static sw_err_t sim_motor_set_output(int id, int speed_ref)
{
    if ((id < 0) || (id >= SIM_MOTOR_SLOT_COUNT))
    {
        return SW_ERR_PARAM;
    }

    s_speed_ref[id] = speed_ref;
    LOG_INFO("hal_motor_sim: id=%d speed_ref=%d", id, speed_ref);
    return SW_OK;
}

static bool sim_motor_at_fwd_limit(int id)
{
    if (id == SIM_MOTOR_GANTRY_ID)
    {
        return m8_signal_is_active(M8_SIG_GANTRY_FWD_LIM);
    }
    return false;
}

static bool sim_motor_at_rev_limit(int id)
{
    if (id == SIM_MOTOR_GANTRY_ID)
    {
        return m8_signal_is_active(M8_SIG_GANTRY_REV_LIM);
    }
    return false;
}

static bool sim_motor_encoder_counter_online(int id)
{
    return (id == SIM_MOTOR_GANTRY_ID);
}

static sw_err_t sim_motor_read_hw_pulse(int id, uint32_t *p_value)
{
    if ((id != SIM_MOTOR_GANTRY_ID) || (p_value == NULL))
    {
        return SW_ERR_PARAM;
    }

    return sim_encoder_counter_read(id, p_value);
}

static sw_err_t sim_motor_clear_hw_pulse(int id)
{
    if (id != SIM_MOTOR_GANTRY_ID)
    {
        return SW_ERR_PARAM;
    }

    return sim_encoder_counter_clear(id);
}

static sw_err_t sim_motor_read_current(int id, uint16_t *p_current)
{
    (void)id;
    (void)p_current;
    return SW_ERR_PARAM;
}

static sw_err_t sim_motor_read_status(int id, uint16_t *p_status)
{
    (void)id;
    (void)p_status;
    return SW_ERR_PARAM;
}

static const hal_motor_ops_t s_ops = {
    .set_output             = sim_motor_set_output,
    .at_fwd_limit           = sim_motor_at_fwd_limit,
    .at_rev_limit           = sim_motor_at_rev_limit,
    .encoder_counter_online = sim_motor_encoder_counter_online,
    .read_hw_pulse          = sim_motor_read_hw_pulse,
    .clear_hw_pulse         = sim_motor_clear_hw_pulse,
    .read_current           = sim_motor_read_current,
    .read_status            = sim_motor_read_status,
};

void hal_motor_sim_register(void)
{
    for (int i = 0; i < SIM_MOTOR_SLOT_COUNT; i++)
    {
        s_speed_ref[i] = 0;
    }
    sim_encoder_counter_reset_all();
    hal_motor_register(&s_ops);
    LOG_INFO("hal_motor_sim: registered");
}
