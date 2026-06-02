/**
 * @file    hal_sensor_sim.c
 * @brief   传感器 HAL 仿真实现（PC 调试 / 场景测试）
 * @author  胡望伟
 * @date    2026-04-10
 *
 * @note    DI 电平写入 hal_io_sim，稳定态由 m8_signal_filter 统一产出。
 *          限位/急停查询与真机一致，均读取滤波后的 m8_signal_is_active()。
 */

#include "adapters/hal/sim_hw/hal_sensor_sim.h"
#include "adapters/hal/sim_hw/hal_io_sim.h"
#include "adapters/hal/sim_hw/sim_encoder_counter.h"
#include "adapters/machine/m8/m8_signal_filter.h"
#include "config/machine/m8_signal_table.h"
#include "ports/hal/hal_sensor_port.h"
#include "common/log.h"

static void sim_sync_signal_di(m8_signal_id_t sig_id, bool active)
{
    const m8_signal_cfg_t *cfg = &m8_signal_table[(int)sig_id];

    hal_io_sim_set_di_level(cfg->io_id, cfg->active_low ? (!active) : active);
}

static void sim_apply_default_di_levels(void)
{
    for (int i = 0; i < M8_SIGNAL_TABLE_SIZE; ++i)
    {
        sim_sync_signal_di((m8_signal_id_t)i, false);
    }
}

static bool sim_gantry_at_fwd_limit(void)
{
    return m8_signal_is_active(M8_SIG_GANTRY_FWD_LIM);
}

static bool sim_gantry_at_rev_limit(void)
{
    return m8_signal_is_active(M8_SIG_GANTRY_REV_LIM);
}

static bool sim_lift_at_top(void)
{
    return m8_signal_is_active(M8_SIG_LIFT_UP_LIM);
}

static bool sim_lift_at_bottom(void)
{
    return m8_signal_is_active(M8_SIG_LIFT_DOWN_LIM);
}

static bool sim_is_estop_active(void)
{
    return m8_signal_is_active(M8_SIG_ESTOP);
}

static void sim_poll_input_events(void)
{
    /* 编码器等非表驱动 DI 事件可在此扩展；限位/急停由 signal_filter 处理。 */
}

static sw_err_t sim_get_vfd_fault_code(hal_vfd_id_t vfd_id, uint16_t *p_code)
{
    (void)vfd_id;
    if (p_code == NULL)
    {
        return SW_ERR_PARAM;
    }
    *p_code = 0U;
    return SW_OK;
}

static const hal_sensor_ops_t s_ops = {
    .gantry_at_fwd_limit  = sim_gantry_at_fwd_limit,
    .gantry_at_rev_limit  = sim_gantry_at_rev_limit,
    .lift_at_top          = sim_lift_at_top,
    .lift_at_bottom       = sim_lift_at_bottom,
    .is_estop_active      = sim_is_estop_active,
    .poll_input_events    = sim_poll_input_events,
    .get_vfd_fault_code   = sim_get_vfd_fault_code,
};

void hal_sensor_sim_register(void)
{
    sim_apply_default_di_levels();
    hal_sensor_register(&s_ops);
    LOG_INFO("hal_sensor_sim: registered");
}

void hal_sensor_sim_set_fwd_limit(bool active)
{
    sim_sync_signal_di(M8_SIG_GANTRY_FWD_LIM, active);
}

void hal_sensor_sim_set_rev_limit(bool active)
{
    sim_sync_signal_di(M8_SIG_GANTRY_REV_LIM, active);
}

void hal_sensor_sim_set_lift_top(bool active)
{
    sim_sync_signal_di(M8_SIG_LIFT_UP_LIM, active);
}

void hal_sensor_sim_set_lift_bottom(bool active)
{
    sim_sync_signal_di(M8_SIG_LIFT_DOWN_LIM, active);
}

void hal_sensor_sim_set_estop(bool active)
{
    sim_sync_signal_di(M8_SIG_ESTOP, active);
}

void hal_sensor_sim_encoder_tick(int delta)
{
    sim_encoder_counter_add_pulse(0, delta);
}
