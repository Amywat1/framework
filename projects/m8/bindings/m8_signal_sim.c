/**
 * @file    m8_signal_sim.c
 * @brief   M8 信号仿真注入实现
 * @author  HUWANGWEI
 * @date    2026-06-07
 */

#ifdef BUILD_SIM

#include "projects/m8/bindings/m8_signal_sim.h"
#include "framework/adapters/outbound/hal/sim_hw/hal_io_sim.h"

static void sync_signal_di(m8_signal_id_t sig_id, bool active)
{
    const m8_signal_cfg_t *cfg;

    if (((int)sig_id < 0) || ((int)sig_id >= M8_SIGNAL_TABLE_SIZE))
    {
        return;
    }

    cfg = &m8_signal_table[(int)sig_id];
    hal_io_sim_set_di_level(cfg->io_id, cfg->active_low ? (!active) : active);
}

void m8_signal_sim_set_active(m8_signal_id_t sig_id, bool active)
{
    sync_signal_di(sig_id, active);
}

void m8_signal_sim_set_fwd_limit(bool active)
{
    m8_signal_sim_set_active(M8_SIG_GANTRY_FWD_LIM, active);
}

void m8_signal_sim_set_rev_limit(bool active)
{
    m8_signal_sim_set_active(M8_SIG_GANTRY_REV_LIM, active);
}

void m8_signal_sim_set_lift_top(bool active)
{
    m8_signal_sim_set_active(M8_SIG_LIFT_UP_LIM, active);
}

void m8_signal_sim_set_lift_bottom(bool active)
{
    m8_signal_sim_set_active(M8_SIG_LIFT_DOWN_LIM, active);
}

void m8_signal_sim_set_estop(bool active)
{
    m8_signal_sim_set_active(M8_SIG_ESTOP, active);
}

void m8_signal_sim_set_rear_lock_home(bool active)
{
    m8_signal_sim_set_active(M8_SIG_REAR_LOCK_HOME, active);
}

void m8_signal_sim_reset_all(void)
{
    for (int i = 0; i < M8_SIGNAL_TABLE_SIZE; ++i)
    {
        m8_signal_sim_set_active((m8_signal_id_t)i, false);
    }
}

#endif /* BUILD_SIM */
